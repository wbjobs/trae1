class AStarPathPlanner {
  constructor(noFlyZoneManager) {
    this.noFlyZoneManager = noFlyZoneManager;
    this.gridSize = 5;
    this.maxIterations = 1000;
  }

  planPath(start, goal, safetyMargin = 2) {
    const startTime = Date.now();
    
    if (this.noFlyZoneManager.checkCollision(goal, safetyMargin)) {
      return { success: false, error: '目标点在禁飞区内' };
    }

    if (this.noFlyZoneManager.checkCollision(start, safetyMargin)) {
      return { success: false, error: '起点在禁飞区内' };
    }

    const directPath = this.checkDirectPath(start, goal, safetyMargin);
    if (directPath) {
      const planningTime = Date.now() - startTime;
      return {
        success: true,
        path: [start, goal],
        planningTime,
        method: 'direct'
      };
    }

    const result = this.aStarSearch(start, goal, safetyMargin);
    const planningTime = Date.now() - startTime;

    if (result.success) {
      const smoothedPath = this.smoothPath(result.path, safetyMargin);
      return {
        success: true,
        path: smoothedPath,
        planningTime,
        method: 'a_star',
        iterations: result.iterations
      };
    }

    return {
      success: false,
      error: result.error || '无法找到可行路径',
      planningTime
    };
  }

  checkDirectPath(start, goal, safetyMargin) {
    const steps = this.calculateSteps(start, goal, this.gridSize / 2);
    
    for (let i = 0; i <= steps; i++) {
      const t = i / steps;
      const point = {
        lat: start.lat + (goal.lat - start.lat) * t,
        lng: start.lng + (goal.lng - start.lng) * t,
        alt: start.alt + (goal.alt - start.alt) * t
      };

      if (this.noFlyZoneManager.checkCollision(point, safetyMargin)) {
        return false;
      }
    }
    return true;
  }

  calculateSteps(start, goal, stepSize) {
    const dx = (goal.lng - start.lng) * 111000 * Math.cos(start.lat * Math.PI / 180);
    const dy = (goal.lat - start.lat) * 111000;
    const dz = (goal.alt || 0) - (start.alt || 0);
    
    const distance = Math.sqrt(dx * dx + dy * dy + dz * dz);
    return Math.max(10, Math.ceil(distance / stepSize));
  }

  aStarSearch(start, goal, safetyMargin) {
    const openSet = [];
    const closedSet = new Set();
    const cameFrom = new Map();
    const gScore = new Map();
    const fScore = new Map();

    const startKey = this.getNodeKey(start);
    gScore.set(startKey, 0);
    fScore.set(startKey, this.heuristic(start, goal));
    openSet.push({ node: start, f: fScore.get(startKey) });

    let iterations = 0;

    while (openSet.length > 0 && iterations < this.maxIterations) {
      iterations++;
      
      openSet.sort((a, b) => a.f - b.f);
      const current = openSet.shift();
      const currentKey = this.getNodeKey(current.node);

      if (this.isGoalReached(current.node, goal)) {
        return {
          success: true,
          path: this.reconstructPath(cameFrom, current.node),
          iterations
        };
      }

      closedSet.add(currentKey);

      const neighbors = this.getNeighbors(current.node, goal);
      
      for (const neighbor of neighbors) {
        const neighborKey = this.getNodeKey(neighbor);
        
        if (closedSet.has(neighborKey)) continue;
        if (this.noFlyZoneManager.checkCollision(neighbor, safetyMargin)) continue;

        const tentativeG = gScore.get(currentKey) + this.distance(current.node, neighbor);

        if (!gScore.has(neighborKey) || tentativeG < gScore.get(neighborKey)) {
          cameFrom.set(neighborKey, { node: current.node, key: currentKey });
          gScore.set(neighborKey, tentativeG);
          
          const h = this.heuristic(neighbor, goal);
          const f = tentativeG + h;
          fScore.set(neighborKey, f);

          const existing = openSet.find(n => this.getNodeKey(n.node) === neighborKey);
          if (!existing) {
            openSet.push({ node: neighbor, f });
          } else {
            existing.f = f;
          }
        }
      }
    }

    return {
      success: false,
      error: iterations >= this.maxIterations ? '达到最大迭代次数' : '开放集已空',
      iterations
    };
  }

  getNodeKey(node) {
    const scale = 100000;
    return `${Math.round(node.lat * scale)},${Math.round(node.lng * scale)},${Math.round((node.alt || 0) * 10)}`;
  }

  heuristic(a, b) {
    return this.distance(a, b);
  }

  distance(a, b) {
    const dx = (b.lng - a.lng) * 111000 * Math.cos(a.lat * Math.PI / 180);
    const dy = (b.lat - a.lat) * 111000;
    const dz = (b.alt || 0) - (a.alt || 0);
    return Math.sqrt(dx * dx + dy * dy + dz * dz);
  }

  getNeighbors(current, goal) {
    const neighbors = [];
    const latStep = this.gridSize / 111000;
    const lngStep = this.gridSize / (111000 * Math.cos(current.lat * Math.PI / 180));
    const altStep = this.gridSize;

    const directions = [
      [0, 1, 0], [0, -1, 0], [1, 0, 0], [-1, 0, 0],
      [1, 1, 0], [1, -1, 0], [-1, 1, 0], [-1, -1, 0],
      [0, 0, 1], [0, 0, -1],
      [1, 0, 1], [1, 0, -1], [-1, 0, 1], [-1, 0, -1],
      [0, 1, 1], [0, 1, -1], [0, -1, 1], [0, -1, -1]
    ];

    for (const [dLat, dLng, dAlt] of directions) {
      const neighbor = {
        lat: current.lat + dLat * latStep,
        lng: current.lng + dLng * lngStep,
        alt: (current.alt || 0) + dAlt * altStep
      };
      neighbors.push(neighbor);
    }

    const toGoal = {
      lat: current.lat + (goal.lat - current.lat) * 0.1,
      lng: current.lng + (goal.lng - current.lng) * 0.1,
      alt: (current.alt || 0) + ((goal.alt || 0) - (current.alt || 0)) * 0.1
    };
    neighbors.push(toGoal);

    return neighbors;
  }

  isGoalReached(current, goal) {
    const tolerance = this.gridSize * 0.8;
    return this.distance(current, goal) <= tolerance;
  }

  reconstructPath(cameFrom, current) {
    const path = [current];
    let currentKey = this.getNodeKey(current);
    
    while (cameFrom.has(currentKey)) {
      const prev = cameFrom.get(currentKey);
      path.unshift(prev.node);
      currentKey = prev.key;
    }
    
    return path;
  }

  smoothPath(path, safetyMargin) {
    if (path.length <= 2) return path;

    const smoothed = [path[0]];
    let i = 0;

    while (i < path.length - 1) {
      let j = path.length - 1;
      
      while (j > i + 1) {
        if (this.checkDirectPath(path[i], path[j], safetyMargin * 0.8)) {
          break;
        }
        j--;
      }
      
      smoothed.push(path[j]);
      i = j;
    }

    return smoothed;
  }

  planFormationPath(center, goal, formationType, spacing, safetyMargin = 3) {
    const result = this.planPath(center, goal, safetyMargin);
    
    if (!result.success) {
      return result;
    }

    const formationPositions = [];
    for (let i = 0; i < result.path.length; i++) {
      const waypoint = result.path[i];
      const positions = this.calculateFormationPoints(waypoint, formationType, spacing, 5);
      formationPositions.push({
        waypoint,
        formation: positions
      });
    }

    return {
      ...result,
      formationPath: formationPositions
    };
  }

  calculateFormationPoints(center, type, spacing, count) {
    const positions = [];
    const latOffset = (meters) => meters / 111000;
    const lngOffset = (meters, lat) => meters / (111000 * Math.cos(lat * Math.PI / 180));

    switch (type) {
      case 'line':
        for (let i = 0; i < count; i++) {
          const offset = (i - (count - 1) / 2) * spacing;
          positions.push({
            lat: center.lat,
            lng: center.lng + lngOffset(offset, center.lat),
            alt: center.alt
          });
        }
        break;

      case 'v_shape':
        const angle = Math.PI / 4;
        for (let i = 0; i < count; i++) {
          if (i === 0) {
            positions.push({ ...center });
          } else {
            const side = i % 2 === 0 ? 1 : -1;
            const row = Math.floor((i + 1) / 2);
            positions.push({
              lat: center.lat + latOffset(row * spacing * Math.cos(angle)),
              lng: center.lng + lngOffset(side * row * spacing * Math.sin(angle), center.lat),
              alt: center.alt
            });
          }
        }
        break;

      case 'circle':
        const radius = spacing * count / (2 * Math.PI);
        for (let i = 0; i < count; i++) {
          const angle = (2 * Math.PI * i) / count;
          positions.push({
            lat: center.lat + latOffset(radius * Math.cos(angle)),
            lng: center.lng + lngOffset(radius * Math.sin(angle), center.lat),
            alt: center.alt
          });
        }
        break;

      case 'triangle':
        let row = 0, col = 0, rowCount = 1;
        for (let i = 0; i < count; i++) {
          positions.push({
            lat: center.lat + latOffset(row * spacing * 0.866),
            lng: center.lng + lngOffset((col - (rowCount - 1) / 2) * spacing, center.lat),
            alt: center.alt
          });
          col++;
          if (col >= rowCount) {
            row++;
            col = 0;
            rowCount++;
          }
        }
        break;
    }

    return positions;
  }
}

module.exports = AStarPathPlanner;
