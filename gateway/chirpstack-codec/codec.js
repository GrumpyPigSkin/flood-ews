// ChirpStack v4 codec for raft/sensor uplinks.

var VALIDITY = {0: 'GOOD', 1: 'INVALID', 2: 'QUESTIONABLE'};

function rdU16LE(b, o) {
  return b[o] | (b[o + 1] << 8);
}

function rdU32LE(b, o) {
  return ((b[o] | (b[o + 1] << 8) | (b[o + 2] << 16) | (b[o + 3] << 24)) >>> 0);
}


// 64 Bit values can act a bit weird on JS, GO and Flutter, so it is better to
// turn it into a string and send that instead.
function rdU64LE(b, o) {
  var low = BigInt(rdU32LE(b, o));
  var high = BigInt(rdU32LE(b, o + 4));
  return ((high << 32n) | low)
      .toString();  // Safe string translation for EUI-64 values
}

const HEADER_SIZE = 22;
const ENTRY_SIZE = 16

function decodeUplink(input) {
  var b = input.bytes;
  var fPort = input.fPort;
  var errors = [];

  // Minimum size check
  if (b.length < HEADER_SIZE) {
    return {data: {}, errors: ['payload shorter than 18-byte header']};
  }

  var node_id = rdU64LE(b, 0);
  var raft_term = rdU32LE(b, 8);
  var raft_log_index = rdU32LE(b, 12);
  var seq_id = rdU32LE(b, 16);
  var alert = b[20] === 1;
  var count = b[21];

  // Check the length is expected given the count.
  var expectedLen = HEADER_SIZE + count * ENTRY_SIZE;
  if (b.length < expectedLen) {
    errors.push(
        'payload too short: count=' + count + ' expects ' + expectedLen +
        ' bytes, got ' + b.length);
    // Dynamically clamp count based on 16-byte Entry boundaries
    count = Math.floor((b.length - HEADER_SIZE) / ENTRY_SIZE);
    if (count < 0) count = 0;
  }

  var entries = [];
  for (var i = 0; i < count; i++) {
    // Array offset starts right after the 18-byte header block
    var off = HEADER_SIZE + i * ENTRY_SIZE;

    var validityCode = b[off + 10];
    var detail = b[off + 11];

    entries.push({
      device_eui: rdU64LE(b, off),          // Bytes 0-7
      water_level_mm: rdU16LE(b, off + 8),  // Bytes 8-9
      validity: VALIDITY[validityCode] || ('UNKNOWN_' + validityCode),
      validity_code: validityCode,  // Byte 10
      outlier: (detail & 0x01) !== 0,
      detail: detail,                   // Byte 11
      timestamp: rdU32LE(b, off + 12),  // Bytes 12-15
    });
  }

  return {
    data: {
      node_id: node_id,
      raft_term: raft_term,
      raft_log_index: raft_log_index,
      seq: seq_id,
      alert: alert,
      count: count,
      entries: entries,
    },
    errors: errors.length > 0 ? errors : undefined,
    warnings: fPort !== undefined && fPort !== 1 ?
        ['unexpected fPort ' + fPort] :
        undefined,
  };
}

function encodeDownlink(input) {
  return {bytes: [], fPort: 1};
}
