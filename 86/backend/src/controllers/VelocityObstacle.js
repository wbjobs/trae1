class VelocityObstacle {
  constructor() {
    this.timeHorizon = 5;
    this.collisionRadius = 3;
    this.maxSpeed = 10;
  }

  computeAvoidanceVelocity(ownPosition, ownVelocity, obstacles, desiredVelocity) {
    const startTime = Date.now();
    
    const voCones = this.computeVOCones(ownPosition, obstacles);
    
    const feasibleVelocity = this.findFeasibleVelocity(
      ownVelocity,
      desiredVelocity,
      voCones
    );
    
    const computationTime = Date.now() - startTime;
    
    return {
      velocity: feasibleVelocity,
      computationTime,
      collisionRisk: this.assessCollisionRisk(ownPosition, obstacles),
      avoidanceNeeded: this.isAvoidanceNeeded(ownVelocity, feasibleVelocity)
    };
  }

  computeVOCones(ownPosition, obstacles) {
    const cones = [];
    
    for (const obstacle of obstacles) {
      const cone = this.computeVOCone(ownPosition, obstacle);
      if (cone) {
        cones.push(cone);
      }
    }
    
    return cones;
  }

  computeVOCone(ownPosition, obstacle) {
    const dx = (obstacle.position.lng - ownPosition.lng) * 111000 * Math.cos(ownPosition.lat * Math.PI / 180);
    const dy = (obstacle.position.lat - ownPosition.lat) * 111000;
    const dz = (obstacle.position.alt || 0) - (ownPosition.alt || 0);
    
    const distance = Math.sqrt(dx * dx + dy * dy + dz * dz);
    const combinedRadius = this.collisionRadius + (obstacle.radius || 2);
    
    if (distance > 50) return null;
    
    const relPos = { x: dx, y: dy, z: dz };
    const relVel = {
      x: (obstacle.velocity?.x || 0) - 0,
      y: (obstacle.velocity?.y || 0) - 0,
      z: (obstacle.velocity?.z || 0) - 0
    };
    
    const sinAlpha = combinedRadius / distance;
    const alpha = Math.asin(Math.min(1, sinAlpha));
    
    const direction = {
      x: relPos.x / distance,
      y: relPos.y / distance,
      z: relPos.z / distance
    };
    
    return {
      obstacle,
      relPos,
      relVel,
      direction,
      alpha,
      distance,
      combinedRadius,
      timeToCollision: this.computeTimeToCollision(relPos, relVel, combinedRadius)
    };
  }

  computeTimeToCollision(relPos, relVel, radius) {
    const a = relVel.x * relVel.x + relVel.y * relVel.y + relVel.z * relVel.z;
    const b = 2 * (relPos.x * relVel.x + relPos.y * relVel.y + relPos.z * relVel.z);
    const c = relPos.x * relPos.x + relPos.y * relPos.y + relPos.z * relPos.z - radius * radius;
    
    const discriminant = b * b - 4 * a * c;
    
    if (discriminant < 0) return Infinity;
    
    const t1 = (-b - Math.sqrt(discriminant)) / (2 * a);
    const t2 = (-b + Math.sqrt(discriminant)) / (2 * a);
    
    if (t1 > 0) return t1;
    if (t2 > 0) return t2;
    
    return 0;
  }

  findFeasibleVelocity(currentVel, desiredVel, voCones) {
    if (voCones.length === 0) {
      return desiredVel;
    }
    
    if (!this.isVelocityInCollision(desiredVel, voCones)) {
      return desiredVel;
    }
    
    let bestVel = { ...currentVel };
    let bestScore = -Infinity;
    
    const candidates = this.generateVelocityCandidates(currentVel, desiredVel, voCones);
    
    for (const candidate of candidates) {
      if (!this.isVelocityInCollision(candidate, voCones)) {
        const score = this.evaluateVelocity(candidate, desiredVel);
        if (score > bestScore) {
          bestScore = score;
          bestVel = candidate;
        }
      }
    }
    
    if (bestScore === -Infinity) {
      bestVel = this.computeEmergencyAvoidance(currentVel, voCones);
    }
    
    return bestVel;
  }

  generateVelocityCandidates(currentVel, desiredVel, voCones) {
    const candidates = [];
    
    candidates.push({ ...desiredVel });
    
    for (let i = 0; i < 8; i++) {
      const angle = (i * Math.PI) / 4;
      const speed = 2;
      candidates.push({
        x: currentVel.x + Math.cos(angle) * speed,
        y: currentVel.y + Math.sin(angle) * speed,
        z: currentVel.z
      });
    }
    
    for (let speed = 1; speed <= this.maxSpeed; speed += 1) {
      candidates.push({
        x: desiredVel.x * speed / this.maxSpeed,
        y: desiredVel.y * speed / this.maxSpeed,
        z: desiredVel.z
      });
    }
    
    for (const cone of voCones) {
      const escapeVel = this.computeEscapeVelocity(cone);
      candidates.push(escapeVel);
    }
    
    return candidates;
  }

  computeEscapeVelocity(cone) {
    const escapeDir = {
      x: -cone.direction.x,
      y: -cone.direction.y,
      z: -cone.direction.z
    };
    
    const speed = Math.min(this.maxSpeed, cone.distance / this.timeHorizon + 1);
    
    return {
      x: escapeDir.x * speed,
      y: escapeDir.y * speed,
      z: escapeDir.z * speed * 0.5
    };
  }

  isVelocityInCollision(velocity, voCones) {
    for (const cone of voCones) {
      if (this.isVelocityInCone(velocity, cone)) {
        return true;
      }
    }
    return false;
  }

  isVelocityInCone(velocity, cone) {
    const relVel = {
      x: velocity.x - (cone.obstacle.velocity?.x || 0),
      y: velocity.y - (cone.obstacle.velocity?.y || 0),
      z: velocity.z - (cone.obstacle.velocity?.z || 0)
    };
    
    const relVelMag = Math.sqrt(relVel.x * relVel.x + relVel.y * relVel.y + relVel.z * relVel.z);
    if (relVelMag < 0.01) return false;
    
    const relVelNorm = {
      x: relVel.x / relVelMag,
      y: relVel.y / relVelMag,
      z: relVel.z / relVelMag
    };
    
    const dot = relVelNorm.x * cone.direction.x + 
                relVelNorm.y * cone.direction.y + 
                relVelNorm.z * cone.direction.z;
    
    const angle = Math.acos(Math.max(-1, Math.min(1, dot)));
    
    return angle < cone.alpha && cone.timeToCollision < this.timeHorizon;
  }

  evaluateVelocity(candidate, desired) {
    const dx = candidate.x - desired.x;
    const dy = candidate.y - desired.y;
    const dz = candidate.z - desired.z;
    const deviation = Math.sqrt(dx * dx + dy * dy + dz * dz);
    
    const speed = Math.sqrt(candidate.x * candidate.x + candidate.y * candidate.y + candidate.z * candidate.z);
    const speedPenalty = Math.max(0, speed - this.maxSpeed);
    
    return -deviation - speedPenalty * 10;
  }

  computeEmergencyAvoidance(currentVel, voCones) {
    let avoidance = { x: 0, y: 0, z: 0 };
    
    for (const cone of voCones) {
      const weight = 1 / Math.max(1, cone.distance);
      avoidance.x -= cone.direction.x * weight * 5;
      avoidance.y -= cone.direction.y * weight * 5;
      avoidance.z += cone.direction.z * weight * 2;
    }
    
    const mag = Math.sqrt(avoidance.x * avoidance.x + avoidance.y * avoidance.y + avoidance.z * avoidance.z);
    if (mag > 0) {
      avoidance.x = (avoidance.x / mag) * this.maxSpeed * 0.8;
      avoidance.y = (avoidance.y / mag) * this.maxSpeed * 0.8;
      avoidance.z = (avoidance.z / mag) * this.maxSpeed * 0.5;
    }
    
    return avoidance;
  }

  assessCollisionRisk(position, obstacles) {
    let maxRisk = 0;
    let nearestObstacle = null;
    let minDistance = Infinity;
    
    for (const obstacle of obstacles) {
      const dx = (obstacle.position.lng - position.lng) * 111000 * Math.cos(position.lat * Math.PI / 180);
      const dy = (obstacle.position.lat - position.lat) * 111000;
      const dz = (obstacle.position.alt || 0) - (position.alt || 0);
      
      const distance = Math.sqrt(dx * dx + dy * dy + dz * dz);
      const combinedRadius = this.collisionRadius + (obstacle.radius || 2);
      
      if (distance < minDistance) {
        minDistance = distance;
        nearestObstacle = obstacle;
      }
      
      if (distance < combinedRadius * 5) {
        const risk = 1 - (distance - combinedRadius) / (combinedRadius * 4);
        maxRisk = Math.max(maxRisk, risk);
      }
    }
    
    return {
      level: maxRisk < 0.3 ? 'low' : maxRisk < 0.7 ? 'medium' : 'high',
      value: maxRisk,
      nearestDistance: minDistance,
      nearestObstacle
    };
  }

  isAvoidanceNeeded(currentVel, newVel) {
    const dx = currentVel.x - newVel.x;
    const dy = currentVel.y - newVel.y;
    const dz = currentVel.z - newVel.z;
    const diff = Math.sqrt(dx * dx + dy * dy + dz * dz);
    return diff > 0.5;
  }

  predictTrajectory(position, velocity, timeSteps = 20, dt = 0.5) {
    const trajectory = [];
    let currentPos = { ...position };
    
    for (let i = 0; i < timeSteps; i++) {
      const step = {
        lat: currentPos.lat + (velocity.y * dt) / 111000,
        lng: currentPos.lng + (velocity.x * dt) / (111000 * Math.cos(currentPos.lat * Math.PI / 180)),
        alt: (currentPos.alt || 0) + velocity.z * dt,
        time: i * dt
      };
      trajectory.push(step);
      currentPos = step;
    }
    
    return trajectory;
  }

  checkFutureCollision(ownTrajectory, obstacle, timeHorizon = 5) {
    for (const point of ownTrajectory) {
      if (point.time > timeHorizon) break;
      
      const obsFuturePos = {
        lat: obstacle.position.lat + ((obstacle.velocity?.y || 0) * point.time) / 111000,
        lng: obstacle.position.lng + ((obstacle.velocity?.x || 0) * point.time) / (111000 * Math.cos(obstacle.position.lat * Math.PI / 180)),
        alt: (obstacle.position.alt || 0) + (obstacle.velocity?.z || 0) * point.time
      };
      
      const dx = (point.lng - obsFuturePos.lng) * 111000 * Math.cos(point.lat * Math.PI / 180);
      const dy = (point.lat - obsFuturePos.lat) * 111000;
      const dz = (point.alt || 0) - (obsFuturePos.alt || 0);
      const distance = Math.sqrt(dx * dx + dy * dy + dz * dz);
      
      if (distance < this.collisionRadius + (obstacle.radius || 2)) {
        return { collision: true, time: point.time, point };
      }
    }
    
    return { collision: false };
  }
}

module.exports = VelocityObstacle;
