const fs = require('fs');
const path = require('path');

const LOG_DIR = path.join(__dirname, '../../logs');
const MAX_LOG_SIZE = 10 * 1024 * 1024;
const MAX_LOG_FILES = 10;

if (!fs.existsSync(LOG_DIR)) {
  fs.mkdirSync(LOG_DIR, { recursive: true });
}

class Logger {
  constructor(name) {
    this.name = name;
    this.logFile = path.join(LOG_DIR, `${name}.log`);
    this.errorFile = path.join(LOG_DIR, `${name}-error.log`);
  }

  getTimestamp() {
    return new Date().toISOString();
  }

  formatMessage(level, message, data) {
    const base = `[${this.getTimestamp()}] [${level}] [${this.name}] ${message}`;
    if (data !== undefined) {
      return `${base} ${typeof data === 'object' ? JSON.stringify(data) : data}`;
    }
    return base;
  }

  rotateLog(filePath) {
    try {
      if (fs.existsSync(filePath)) {
        const stat = fs.statSync(filePath);
        if (stat.size > MAX_LOG_SIZE) {
          const timestamp = Date.now();
          const rotatedPath = `${filePath}.${timestamp}`;
          fs.renameSync(filePath, rotatedPath);

          const logFiles = fs.readdirSync(LOG_DIR)
            .filter(f => f.startsWith(this.name))
            .sort();
          
          while (logFiles.length > MAX_LOG_FILES) {
            const oldestFile = logFiles.shift();
            fs.unlinkSync(path.join(LOG_DIR, oldestFile));
          }
        }
      }
    } catch (error) {
      console.error('Log rotation error:', error);
    }
  }

  writeToFile(filePath, message) {
    try {
      this.rotateLog(filePath);
      fs.appendFileSync(filePath, message + '\n');
    } catch (error) {
      console.error('Write log error:', error);
    }
  }

  info(message, data) {
    const formatted = this.formatMessage('INFO', message, data);
    console.log(formatted);
    this.writeToFile(this.logFile, formatted);
  }

  warn(message, data) {
    const formatted = this.formatMessage('WARN', message, data);
    console.warn(formatted);
    this.writeToFile(this.logFile, formatted);
  }

  error(message, data) {
    const formatted = this.formatMessage('ERROR', message, data);
    console.error(formatted);
    this.writeToFile(this.errorFile, formatted);
  }

  debug(message, data) {
    if (process.env.NODE_ENV !== 'production') {
      const formatted = this.formatMessage('DEBUG', message, data);
      console.debug(formatted);
    }
  }
}

const loggers = {};

module.exports = {
  getLogger(name = 'app') {
    if (!loggers[name]) {
      loggers[name] = new Logger(name);
    }
    return loggers[name];
  }
};
