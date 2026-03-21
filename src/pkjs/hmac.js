// Self-contained HMAC-SHA1 + Base64 for GTI API authentication
// Minimal implementation suitable for PebbleKit JS runtime

var SHA1 = (function() {
  function rotl(n, s) { return (n << s) | (n >>> (32 - s)); }

  function toBytes(str) {
    var bytes = [];
    for (var i = 0; i < str.length; i++) {
      var c = str.charCodeAt(i);
      if (c < 0x80) {
        bytes.push(c);
      } else if (c < 0x800) {
        bytes.push(0xC0 | (c >> 6), 0x80 | (c & 0x3F));
      } else {
        bytes.push(0xE0 | (c >> 12), 0x80 | ((c >> 6) & 0x3F), 0x80 | (c & 0x3F));
      }
    }
    return bytes;
  }

  function hash(msg) {
    var bytes = (typeof msg === 'string') ? toBytes(msg) : msg;
    var len = bytes.length;

    // Pre-processing: padding
    bytes.push(0x80);
    while ((bytes.length % 64) !== 56) bytes.push(0);

    // Append length in bits as 64-bit big-endian
    var bitLen = len * 8;
    for (var i = 7; i >= 0; i--) {
      bytes.push((bitLen / Math.pow(2, i * 8)) & 0xFF);
    }

    var h0 = 0x67452301;
    var h1 = 0xEFCDAB89;
    var h2 = 0x98BADCFE;
    var h3 = 0x10325476;
    var h4 = 0xC3D2E1F0;

    for (var offset = 0; offset < bytes.length; offset += 64) {
      var w = [];
      for (var j = 0; j < 16; j++) {
        w[j] = (bytes[offset + j * 4] << 24) |
               (bytes[offset + j * 4 + 1] << 16) |
               (bytes[offset + j * 4 + 2] << 8) |
                bytes[offset + j * 4 + 3];
      }
      for (var j = 16; j < 80; j++) {
        w[j] = rotl(w[j-3] ^ w[j-8] ^ w[j-14] ^ w[j-16], 1);
      }

      var a = h0, b = h1, c = h2, d = h3, e = h4;
      for (var j = 0; j < 80; j++) {
        var f, k;
        if (j < 20) {
          f = (b & c) | (~b & d); k = 0x5A827999;
        } else if (j < 40) {
          f = b ^ c ^ d; k = 0x6ED9EBA1;
        } else if (j < 60) {
          f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDC;
        } else {
          f = b ^ c ^ d; k = 0xCA62C1D6;
        }
        var temp = (rotl(a, 5) + f + e + k + w[j]) & 0xFFFFFFFF;
        e = d; d = c; c = rotl(b, 30); b = a; a = temp;
      }
      h0 = (h0 + a) & 0xFFFFFFFF;
      h1 = (h1 + b) & 0xFFFFFFFF;
      h2 = (h2 + c) & 0xFFFFFFFF;
      h3 = (h3 + d) & 0xFFFFFFFF;
      h4 = (h4 + e) & 0xFFFFFFFF;
    }

    // Return as byte array (20 bytes)
    var result = [];
    var vals = [h0, h1, h2, h3, h4];
    for (var i = 0; i < 5; i++) {
      result.push((vals[i] >> 24) & 0xFF);
      result.push((vals[i] >> 16) & 0xFF);
      result.push((vals[i] >> 8) & 0xFF);
      result.push(vals[i] & 0xFF);
    }
    return result;
  }

  return { hash: hash, toBytes: toBytes };
})();

function hmacSHA1(key, message) {
  var keyBytes = SHA1.toBytes(key);

  // If key > 64 bytes, hash it first
  if (keyBytes.length > 64) {
    keyBytes = SHA1.hash(keyBytes);
  }
  // Pad key to 64 bytes
  while (keyBytes.length < 64) keyBytes.push(0);

  var opad = [];
  var ipad = [];
  for (var i = 0; i < 64; i++) {
    opad.push(keyBytes[i] ^ 0x5C);
    ipad.push(keyBytes[i] ^ 0x36);
  }

  var msgBytes = SHA1.toBytes(message);
  var inner = SHA1.hash(ipad.concat(msgBytes));
  return SHA1.hash(opad.concat(inner));
}

function toBase64(bytes) {
  var chars = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/';
  var result = '';
  for (var i = 0; i < bytes.length; i += 3) {
    var b0 = bytes[i];
    var b1 = i + 1 < bytes.length ? bytes[i + 1] : 0;
    var b2 = i + 2 < bytes.length ? bytes[i + 2] : 0;
    result += chars[(b0 >> 2) & 0x3F];
    result += chars[((b0 << 4) | (b1 >> 4)) & 0x3F];
    result += (i + 1 < bytes.length) ? chars[((b1 << 2) | (b2 >> 6)) & 0x3F] : '=';
    result += (i + 2 < bytes.length) ? chars[b2 & 0x3F] : '=';
  }
  return result;
}

function signRequest(password, body) {
  var hmac = hmacSHA1(password, body);
  return toBase64(hmac);
}

module.exports = {
  signRequest: signRequest
};
