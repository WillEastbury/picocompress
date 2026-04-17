// picocompress — test suite (Node.js ESM)
// Run with: node test.mjs

import { compress, decompress, compressBound, PROFILES } from './picocompress.mjs';
import { createRequire } from 'module';

const encoder = new TextEncoder();
const decoder = new TextDecoder();

let passed = 0;
let failed = 0;

function assert(cond, msg) {
  if (!cond) {
    console.error(`  FAIL: ${msg}`);
    failed++;
  } else {
    passed++;
  }
}

function arraysEqual(a, b) {
  if (a.length !== b.length) return false;
  for (let i = 0; i < a.length; i++) {
    if (a[i] !== b[i]) return false;
  }
  return true;
}

function roundtrip(name, data, options) {
  const input = data instanceof Uint8Array ? data : encoder.encode(data);
  const compressed = compress(input, options);
  const decompressed = decompress(compressed);
  const ok = arraysEqual(input, decompressed);
  assert(ok, `${name}: roundtrip mismatch (in=${input.length}, comp=${compressed.length}, out=${decompressed.length})`);
  if (ok) {
    const ratio = input.length > 0 ? ((compressed.length / input.length) * 100).toFixed(1) : '0.0';
    console.log(`  OK: ${name} (${input.length} → ${compressed.length}, ${ratio}%)`);
  }
  return { input, compressed, decompressed, ok };
}

// ---- Tests ----

console.log('\n=== picocompress JavaScript port — test suite ===\n');

// 1. Empty input
console.log('--- Empty input ---');
{
  const c = compress(new Uint8Array(0));
  assert(c.length === 0, 'empty compress');
  const d = decompress(new Uint8Array(0));
  assert(d.length === 0, 'empty decompress');
  console.log('  OK: empty input');
}

// 2. Single byte
console.log('--- Single byte ---');
roundtrip('single byte', new Uint8Array([42]));

// 3. Short strings
console.log('--- Short strings ---');
roundtrip('hello', 'Hello, world!');
roundtrip('abc', 'abc');
roundtrip('json-key', '{"name":"value"}');

// 4. Repeated data (LZ should compress)
console.log('--- Repeated data ---');
roundtrip('repeat-short', 'abcabcabcabcabcabc');
roundtrip('repeat-long', 'The quick brown fox jumps. The quick brown fox jumps. The quick brown fox jumps.');

// 5. JSON payload (dict + LZ)
console.log('--- JSON payload ---');
{
  const json = JSON.stringify({
    name: "test", type: "device", status: "active", value: 42, error: null,
    message: "content request default", code: "string", result: "length",
    data: [{ item: "text", time: "2024-01-01", mode: "input", order: 1 }],
    list: [{ name: "number", size: 100, active: true, region: "us-east" }],
    response: { operator: "default", request: "https://example.com" }
  });
  roundtrip('json-payload', json);
}

// 6. All byte values
console.log('--- All byte values ---');
{
  const allBytes = new Uint8Array(256);
  for (let i = 0; i < 256; i++) allBytes[i] = i;
  roundtrip('all-bytes', allBytes);
}

// 7. Large input (multi-block)
console.log('--- Multi-block ---');
{
  const big = encoder.encode('A'.repeat(2000));
  roundtrip('2000-As', big);
}
{
  const text = 'The quick brown fox jumps over the lazy dog. ';
  const big = encoder.encode(text.repeat(50));
  roundtrip('multi-block-text', big);
}

// 8. Binary data (random-ish)
console.log('--- Binary data ---');
{
  const bin = new Uint8Array(1024);
  let x = 12345;
  for (let i = 0; i < bin.length; i++) {
    x = (x * 1103515245 + 12345) & 0x7FFFFFFF;
    bin[i] = (x >>> 16) & 0xFF;
  }
  roundtrip('pseudo-random-1k', bin);
}

// 9. Exactly one block
console.log('--- Exact block size ---');
roundtrip('exact-508', new Uint8Array(508).fill(0x41));

// 10. Block boundary
console.log('--- Block boundary ---');
roundtrip('509-bytes', new Uint8Array(509).fill(0x42));
roundtrip('1016-bytes', new Uint8Array(1016).fill(0x43));

// 11. Dictionary entries directly
console.log('--- Dictionary entries ---');
roundtrip('dict-true', 'true true true true');
roundtrip('dict-false', 'false false false');
roundtrip('dict-status', 'status status status');
roundtrip('dict-message', 'message content request default');
roundtrip('dict-https', 'https://example.com https://test.org');
roundtrip('dict-operator', 'operator response operator response');

// 12. HTML content
console.log('--- HTML ---');
roundtrip('html', '<div class="content"><div>test</div></div><div>more</div>');

// 13. Profiles
console.log('--- Profiles ---');
{
  const text = encoder.encode('The status of the device is active. The result length is the number value. '.repeat(20));
  for (const name of Object.keys(PROFILES)) {
    roundtrip(`profile-${name}`, text, { profile: name });
  }
}

// 14. compressBound
console.log('--- compressBound ---');
{
  assert(compressBound(0) === 0, 'bound(0)');
  assert(compressBound(1) >= 1, 'bound(1)');
  assert(compressBound(508) >= 508, 'bound(508)');
  assert(compressBound(1000) >= 1000, 'bound(1000)');
  // Verify compressed output fits within bound
  const data = encoder.encode('test data '.repeat(100));
  const bound = compressBound(data.length);
  const comp = compress(data);
  assert(comp.length <= bound, `compressed (${comp.length}) <= bound (${bound})`);
  console.log('  OK: compressBound');
}

// 15. Type validation
console.log('--- Type validation ---');
{
  let threw = false;
  try { compress('not a uint8array'); } catch { threw = true; }
  assert(threw, 'compress rejects non-Uint8Array');
  threw = false;
  try { decompress('not a uint8array'); } catch { threw = true; }
  assert(threw, 'decompress rejects non-Uint8Array');
  console.log('  OK: type validation');
}

// 16. Corrupt data detection
console.log('--- Corrupt data detection ---');
{
  let threw = false;
  try { decompress(new Uint8Array([0x01, 0x00, 0x01, 0x00, 0xFF])); } catch { threw = true; }
  assert(threw, 'detects corrupt compressed data');
  threw = false;
  try { decompress(new Uint8Array([0x01, 0x00])); } catch { threw = true; }
  assert(threw, 'detects truncated header');
  console.log('  OK: corrupt data detection');
}

// 17. CommonJS wrapper
console.log('--- CommonJS wrapper ---');
{
  const require = createRequire(import.meta.url);
  const cjs = require('./picocompress.cjs');
  assert(typeof cjs.compress === 'function', 'CJS compress exists');
  assert(typeof cjs.decompress === 'function', 'CJS decompress exists');
  const inp = encoder.encode('CommonJS test roundtrip');
  const comp = cjs.compress(inp);
  const dec = cjs.decompress(comp);
  assert(arraysEqual(inp, dec), 'CJS roundtrip');
  console.log('  OK: CommonJS wrapper');
}

// 18. Cross-profile compatibility (any encoder, any decoder)
console.log('--- Cross-profile decompression ---');
{
  const text = encoder.encode('{"status":"active","name":"test","value":123,"type":"device","message":"content"}');
  for (const p of Object.keys(PROFILES)) {
    const comp = compress(text, { profile: p });
    const dec = decompress(comp);
    assert(arraysEqual(text, dec), `cross-profile: ${p}`);
  }
  console.log('  OK: all profiles decompress correctly');
}

// ---- Summary ----
console.log(`\n=== Results: ${passed} passed, ${failed} failed ===\n`);
if (failed > 0) process.exit(1);
