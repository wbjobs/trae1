const fs = require('fs');
const path = require('path');
const { createCanvas, loadImage } = require('canvas');

class WatermarkUtil {
  static async addTextWatermark(imageBuffer, options = {}) {
    const {
      text = '安全文件分享系统',
      fontSize = 20,
      color = 'rgba(255, 255, 255, 0.5)',
      position = 'bottom-right',
      opacity = 0.5
    } = options;

    try {
      const image = await loadImage(imageBuffer);
      const canvas = createCanvas(image.width, image.height);
      const ctx = canvas.getContext('2d');

      ctx.drawImage(image, 0, 0);

      ctx.font = `bold ${fontSize}px Arial`;
      ctx.fillStyle = color;
      ctx.globalAlpha = opacity;

      const textWidth = ctx.measureText(text).width;
      const textHeight = fontSize;
      const padding = 20;

      let x, y;
      switch (position) {
        case 'top-left':
          x = padding;
          y = textHeight + padding;
          break;
        case 'top-right':
          x = image.width - textWidth - padding;
          y = textHeight + padding;
          break;
        case 'bottom-left':
          x = padding;
          y = image.height - padding;
          break;
        case 'bottom-right':
        default:
          x = image.width - textWidth - padding;
          y = image.height - padding;
          break;
      }

      ctx.fillText(text, x, y);

      return canvas.toBuffer();
    } catch (error) {
      console.error('添加水印失败:', error);
      return imageBuffer;
    }
  }

  static async addTileWatermark(imageBuffer, options = {}) {
    const {
      text = '机密文件',
      fontSize = 30,
      color = 'rgba(255, 255, 255, 0.3)',
      opacity = 0.3,
      rotate = -45,
      gap = 150
    } = options;

    try {
      const image = await loadImage(imageBuffer);
      const canvas = createCanvas(image.width, image.height);
      const ctx = canvas.getContext('2d');

      ctx.drawImage(image, 0, 0);

      ctx.font = `bold ${fontSize}px Arial`;
      ctx.fillStyle = color;
      ctx.globalAlpha = opacity;
      ctx.textAlign = 'center';
      ctx.textBaseline = 'middle';

      const radians = (rotate * Math.PI) / 180;
      ctx.rotate(radians);

      const diagonal = Math.sqrt(image.width * image.width + image.height * image.height);
      for (let y = -diagonal; y < diagonal; y += gap) {
        for (let x = -diagonal; x < diagonal; x += gap) {
          ctx.fillText(text, x, y);
        }
      }

      return canvas.toBuffer();
    } catch (error) {
      console.error('添加平铺水印失败:', error);
      return imageBuffer;
    }
  }

  static isImageFile(mimeType) {
    return mimeType && mimeType.startsWith('image/');
  }
}

module.exports = WatermarkUtil;
