<?php
/**
 * Sauvegarde PoulpyTube : un seul JSON (scènes + réglages des barres).
 * Chaque envoi garde aussi une copie datée (les 30 dernières).
 *
 * GET  ?action=info              → date et nombre de scènes de la sauvegarde courante
 * GET  ?action=get[&version=…]   → la sauvegarde (courante ou une version)
 * GET  ?action=versions          → les versions disponibles, la plus récente d'abord
 * POST {action:"save", backup:{…}}
 */
header('Content-Type: application/json; charset=utf-8');
header('Access-Control-Allow-Origin: *');
header('Access-Control-Allow-Methods: GET, POST, OPTIONS');
header('Access-Control-Allow-Headers: Content-Type');
header('Cache-Control: no-store');

if ($_SERVER['REQUEST_METHOD'] === 'OPTIONS') { http_response_code(204); exit; }

const MAX_BYTES = 65536;
const KEEP_VERSIONS = 30;
const MAX_SCENES = 20;
const MAX_GROUPS = 16;
const MAX_TUBES = 64;

$dir = __DIR__ . '/../data';
$hist = $dir . '/history';
if (!is_dir($hist)) @mkdir($hist, 0755, true);
$current = $dir . '/current.json';

function bad($msg, $code = 400) {
  http_response_code($code);
  echo json_encode(['ok' => false, 'error' => $msg]);
  exit;
}
function ok($data = []) {
  echo json_encode(array_merge(['ok' => true], $data), JSON_UNESCAPED_UNICODE);
  exit;
}
function clamp_int($v, $lo, $hi) { return max($lo, min($hi, (int)$v)); }
function clean_name($s, $max) {
  $s = trim(preg_replace('/[\x00-\x1F\x7F]/u', '', (string)$s));
  return mb_substr($s, 0, $max);
}
function clean_mac($s) {
  $s = strtoupper((string)$s);
  return preg_match('/^([0-9A-F]{2}:){5}[0-9A-F]{2}$/', $s) ? $s : null;
}
function clean_look($l) {
  if (!is_array($l)) return null;
  $out = [];
  for ($i = 0; $i < 10; $i++) $out[] = clamp_int($l[$i] ?? 0, 0, 255);
  return $out;
}

function normalize_backup($b) {
  if (!is_array($b) || ($b['format'] ?? '') !== 'poulpytube.backup') bad('format');
  $scenes = [];
  $seen = [];
  foreach (array_slice((array)($b['scenes'] ?? []), 0, MAX_SCENES) as $s) {
    if (!is_array($s)) continue;
    $slot = clamp_int($s['slot'] ?? -1, -1, MAX_SCENES - 1);
    if ($slot < 0 || isset($seen[$slot])) continue;
    $seen[$slot] = true;
    $groups = [];
    foreach (array_slice((array)($s['groups'] ?? []), 0, MAX_GROUPS) as $g) {
      $groups[] = clean_look($g) ?? array_fill(0, 10, 0);
    }
    $tubes = [];
    foreach (array_slice((array)($s['tubes'] ?? []), 0, MAX_TUBES) as $t) {
      $mac = clean_mac($t['mac'] ?? '');
      $look = clean_look($t['look'] ?? null);
      if ($mac && $look) $tubes[] = ['mac' => $mac, 'look' => $look];
    }
    $scenes[] = [
      'slot' => $slot,
      'name' => clean_name($s['name'] ?? '', 40),
      'fadeMs' => clamp_int($s['fadeMs'] ?? 1000, 0, 60000),
      'groups' => $groups,
      'tubes' => $tubes,
    ];
  }
  usort($scenes, function ($a, $c) { return $a['slot'] <=> $c['slot']; });

  $setup = [];
  foreach (array_slice((array)($b['tubes'] ?? []), 0, MAX_TUBES) as $t) {
    $mac = clean_mac($t['mac'] ?? '');
    if (!$mac) continue;
    $setup[] = [
      'mac' => $mac,
      'group' => clamp_int($t['group'] ?? 1, 1, MAX_GROUPS),
      'address' => clamp_int($t['address'] ?? 0, 0, 503),
      'leds' => clamp_int($t['leds'] ?? 144, 1, 1000),
      'watts' => clamp_int($t['watts'] ?? 0, 0, 500),
    ];
  }

  $settings = (array)($b['settings'] ?? []);
  return [
    'format' => 'poulpytube.backup',
    'version' => 1,
    'saved' => gmdate('c'),
    'scenes' => $scenes,
    'tubes' => $setup,
    'settings' => ['returnDelay' => clamp_int($settings['returnDelay'] ?? 4, 1, 60)],
  ];
}

function read_json($path) {
  if (!is_file($path)) return null;
  $d = json_decode(@file_get_contents($path), true);
  return is_array($d) ? $d : null;
}

$method = $_SERVER['REQUEST_METHOD'];
$input = null;
if ($method === 'POST') {
  $raw = file_get_contents('php://input', false, null, 0, MAX_BYTES + 1);
  if (strlen($raw) > MAX_BYTES) bad('too_large', 413);
  $input = json_decode($raw, true);
  if (!is_array($input)) bad('bad_json');
  $action = $input['action'] ?? 'save';
} else {
  $action = $_GET['action'] ?? 'info';
}

if ($action === 'info') {
  $b = read_json($current);
  if (!$b) ok(['empty' => true]);
  ok(['empty' => false, 'saved' => $b['saved'] ?? null, 'scenes' => count($b['scenes'] ?? []), 'tubes' => count($b['tubes'] ?? [])]);
}

if ($action === 'versions') {
  $files = glob($hist . '/*.json') ?: [];
  rsort($files);
  ok(['versions' => array_map(function ($f) { return basename($f, '.json'); }, $files)]);
}

if ($action === 'get') {
  $v = $_GET['version'] ?? '';
  if ($v !== '') {
    if (!preg_match('/^\d{8}-\d{6}$/', $v)) bad('version');
    $b = read_json("$hist/$v.json");
  } else {
    $b = read_json($current);
  }
  if (!$b) bad('not_found', 404);
  ok(['backup' => $b]);
}

if ($action === 'save') {
  if ($method !== 'POST') bad('method', 405);
  $b = normalize_backup($input['backup'] ?? null);
  $json = json_encode($b, JSON_UNESCAPED_UNICODE | JSON_PRETTY_PRINT);
  $tmp = $current . '.tmp';
  if (file_put_contents($tmp, $json, LOCK_EX) === false || !rename($tmp, $current)) bad('write_failed', 500);
  @file_put_contents($hist . '/' . gmdate('Ymd-His') . '.json', $json);
  $files = glob($hist . '/*.json') ?: [];
  rsort($files);
  foreach (array_slice($files, KEEP_VERSIONS) as $old) @unlink($old);
  ok(['saved' => $b['saved'], 'scenes' => count($b['scenes'])]);
}

bad('unknown_action');
