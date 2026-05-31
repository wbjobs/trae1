const crypto = require('crypto');

class CryptoUtil {
  static generateKey() {
    return crypto.randomBytes(32).toString('hex');
  }

  static generateIv() {
    return crypto.randomBytes(16).toString('hex');
  }

  static encrypt(buffer, key, iv, algorithm = 'aes-256-cbc') {
    const keyBuffer = Buffer.from(key, 'hex');
    const ivBuffer = Buffer.from(iv, 'hex');
    const cipher = crypto.createCipheriv(algorithm, keyBuffer, ivBuffer);
    const encrypted = Buffer.concat([cipher.update(buffer), cipher.final()]);
    return encrypted;
  }

  static decrypt(buffer, key, iv, algorithm = 'aes-256-cbc') {
    const keyBuffer = Buffer.from(key, 'hex');
    const ivBuffer = Buffer.from(iv, 'hex');
    const decipher = crypto.createDecipheriv(algorithm, keyBuffer, ivBuffer);
    const decrypted = Buffer.concat([decipher.update(buffer), decipher.final()]);
    return decrypted;
  }

  static encryptStream(key, iv, algorithm = 'aes-256-cbc') {
    const keyBuffer = Buffer.from(key, 'hex');
    const ivBuffer = Buffer.from(iv, 'hex');
    return crypto.createCipheriv(algorithm, keyBuffer, ivBuffer);
  }

  static decryptStream(key, iv, algorithm = 'aes-256-cbc') {
    const keyBuffer = Buffer.from(key, 'hex');
    const ivBuffer = Buffer.from(iv, 'hex');
    return crypto.createDecipheriv(algorithm, keyBuffer, ivBuffer);
  }

  static hashPassword(password) {
    return crypto.createHash('sha256').update(password).digest('hex');
  }

  static verifyPassword(password, hash) {
    return this.hashPassword(password) === hash;
  }

  static generateShareCode(length = 8) {
    const chars = 'ABCDEFGHJKLMNPQRSTUVWXYZ23456789';
    let code = '';
    for (let i = 0; i < length; i++) {
      code += chars.charAt(Math.floor(Math.random() * chars.length));
    }
    return code;
  }
}

module.exports = CryptoUtil;
