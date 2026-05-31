const { v4: uuidv4 } = require('uuid');

class NoFlyZoneManager {
  constructor() {
    this.zones = new Map();
    this.dynamicObstacles = new Map();
  }

  addZone(zone) {
    const id = zone.id || uuidv4();
    const newZone = {
      id,
      type: zone.type,
      name: zone.name || `禁飞区 ${this.zones.size + 1}`,
      createdAt: Date.now(),
      ...zone
    };

    this.zones.set(id, newZone);
    return newZone;
  }

  removeZone(zoneId) {
    return this.zones.delete(zoneId);
  }

  getZones() {
    return Array.from(this.zones.values());
  }

  getZone(zoneId) {
    return this.zones.get(zoneId);
  }

  addDynamicObstacle(obstacle) {
    const id = obstacle.id || uuidv4();
    const newObstacle = {
      id,
      type: obstacle.type || 'dynamic',
      position: obstacle.position,
      velocity: obstacle.velocity || { x: 0, y: 0, z: 0 },
      radius: obstacle.radius || 5,
      lastUpdate: Date.now(),
      ...obstacle
    };

    this.dynamicObstacles.set(id, newObstacle);
    return newObstacle;
  }

  updateDynamicObstacle(id, data) {
    const obstacle = this.dynamicObstacles.get(id);
    if (obstacle) {
      Object.assign(obstacle, data, { lastUpdate: Date.now() });
      return obstacle;
    }
    return null;
  }

  removeDynamicObstacle(id) {
    return this.dynamicObstacles.delete(id);
  }

  getDynamicObstacles() {
    const now = Date.now();
    const validObstacles = [];
    
    for (const obstacle of this.dynamicObstacles.values()) {
      if (now - obstacle.lastUpdate < 5000) {
        validObstacles.push(obstacle);
      } else {
        this.dynamicObstacles.delete(obstacle.id);
      }
    }
    
    return validObstacles;
  }

  isPointInZone(point, zone) {
    if (zone.type === 'circle') {
      return this.isPointInCircle(point, zone);
    } else if (zone.type === 'polygon') {
      return this.isPointInPolygon(point, zone);
    }
    return false;
  }

  isPointInCircle(point, circle) {
    const dx = (point.lng - circle.center.lng) * 111000 * Math.cos(point.lat * Math.PI / 180);
    const dy = (point.lat - circle.center.lat) * 111000;
    const dz = (point.alt || 0) - (circle.center.alt || 0);
    
    const distance = Math.sqrt(dx * dx + dy * dy + dz * dz);
    const altitudeOk = this.checkAltitude(point, circle);
    
    return distance <= circle.radius && altitudeOk;
  }

  isPointInPolygon(point, polygon) {
    const { coordinates } = polygon;
    if (!coordinates || coordinates.length < 3) return false;

    let inside = false;
    const x = point.lng;
    const y = point.lat;

    for (let i = 0, j = coordinates.length - 1; i < coordinates.length; j = i++) {
      const xi = coordinates[i].lng;
      const yi = coordinates[i].lat;
      const xj = coordinates[j].lng;
      const yj = coordinates[j].lat;

      if (((yi > y) !== (yj > y)) && (x < (xj - xi) * (y - yi) / (yj - yi) + xi)) {
        inside = !inside;
      }
    }

    const altitudeOk = this.checkAltitude(point, polygon);
    return inside && altitudeOk;
  }

  checkAltitude(point, zone) {
    if (zone.minAlt !== undefined && point.alt < zone.minAlt) {
      return false;
    }
    if (zone.maxAlt !== undefined && point.alt > zone.maxAlt) {
      return false;
    }
    return true;
  }

  isPointInAnyZone(point) {
    for (const zone of this.zones.values()) {
      if (this.isPointInZone(point, zone)) {
        return zone;
      }
    }
    return null;
  }

  checkCollision(point, safetyMargin = 0) {
    const testPoint = { ...point };
    
    if (safetyMargin > 0) {
      const marginLat = safetyMargin / 111000;
      const marginLng = safetyMargin / (111000 * Math.cos(point.lat * Math.PI / 180));
      
      for (let dLat = -1; dLat <= 1; dLat++) {
        for (let dLng = -1; dLng <= 1; dLng++) {
          const p = {
            lat: point.lat + dLat * marginLat,
            lng: point.lng + dLng * marginLng,
            alt: point.alt
          };
          const zone = this.isPointInAnyZone(p);
          if (zone) return zone;
        }
      }
    }

    return this.isPointInAnyZone(testPoint);
  }

  checkPathCollision(path, safetyMargin = 2) {
    for (const point of path) {
      const collision = this.checkCollision(point, safetyMargin);
      if (collision) {
        return { collision, point };
      }
    }
    return null;
  }

  getDistanceToZone(point, zone) {
    if (zone.type === 'circle') {
      const dx = (point.lng - zone.center.lng) * 111000 * Math.cos(point.lat * Math.PI / 180);
      const dy = (point.lat - zone.center.lat) * 111000;
      const distance = Math.sqrt(dx * dx + dy * dy) - zone.radius;
      return Math.max(0, distance);
    } else if (zone.type === 'polygon') {
      return this.getDistanceToPolygon(point, zone);
    }
    return Infinity;
  }

  getDistanceToPolygon(point, polygon) {
    const { coordinates } = polygon;
    if (!coordinates || coordinates.length < 3) return Infinity;

    let minDistance = Infinity;
    
    for (let i = 0; i < coordinates.length; i++) {
      const j = (i + 1) % coordinates.length;
      const distance = this.getDistanceToSegment(
        point,
        coordinates[i],
        coordinates[j]
      );
      minDistance = Math.min(minDistance, distance);
    }

    return minDistance;
  }

  getDistanceToSegment(point, segStart, segEnd) {
    const dx = (segEnd.lng - segStart.lng) * 111000 * Math.cos(point.lat * Math.PI / 180);
    const dy = (segEnd.lat - segStart.lat) * 111000;
    const px = (point.lng - segStart.lng) * 111000 * Math.cos(point.lat * Math.PI / 180);
    const py = (point.lat - segStart.lat) * 111000;

    const lenSq = dx * dx + dy * dy;
    if (lenSq === 0) return Math.sqrt(px * px + py * py);

    let t = Math.max(0, Math.min(1, (px * dx + py * dy) / lenSq));
    const projX = dx * t;
    const projY = dy * t;

    return Math.sqrt((px - projX) ** 2 + (py - projY) ** 2);
  }

  getNearestZone(point) {
    let nearest = null;
    let minDistance = Infinity;

    for (const zone of this.zones.values()) {
      const distance = this.getDistanceToZone(point, zone);
      if (distance < minDistance) {
        minDistance = distance;
        nearest = { zone, distance };
      }
    }

    return nearest;
  }

  clearAll() {
    this.zones.clear();
    this.dynamicObstacles.clear();
  }
}

module.exports = NoFlyZoneManager;