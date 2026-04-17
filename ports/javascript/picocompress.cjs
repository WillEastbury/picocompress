// picocompress — CommonJS wrapper
// Re-exports the ES module API for require() compatibility.

const mod = require('./picocompress.mjs');

module.exports = {
  compress: mod.compress,
  decompress: mod.decompress,
  compressBound: mod.compressBound,
  PROFILES: mod.PROFILES,
  default: mod.default,
};
