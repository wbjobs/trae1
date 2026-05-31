const TerrainTypes = {
    PLAIN: {
        id: 'plain',
        name: '平原',
        color: '#90EE90',
        moveCost: 1,
        defBonus: 0,
        passable: true
    },
    FOREST: {
        id: 'forest',
        name: '森林',
        color: '#228B22',
        moveCost: 2,
        defBonus: 0.2,
        passable: true
    },
    MOUNTAIN: {
        id: 'mountain',
        name: '山地',
        color: '#8B4513',
        moveCost: 3,
        defBonus: 0.4,
        passable: true
    },
    WATER: {
        id: 'water',
        name: '水域',
        color: '#1E90FF',
        moveCost: Infinity,
        defBonus: 0,
        passable: false
    },
    FORTRESS: {
        id: 'fortress',
        name: '要塞',
        color: '#708090',
        moveCost: 1,
        defBonus: 0.5,
        passable: true
    },
    ROAD: {
        id: 'road',
        name: '道路',
        color: '#D2B48C',
        moveCost: 0.5,
        defBonus: -0.1,
        passable: true
    }
};

class MapGenerator {
    constructor(width = 15, height = 12) {
        this.width = width;
        this.height = height;
        this.map = [];
        this.player1Start = [];
        this.player2Start = [];
    }

    generate(seed = null) {
        if (seed) {
            Math.random = this.seededRandom(seed);
        }
        
        this.map = [];
        for (let y = 0; y < this.height; y++) {
            this.map[y] = [];
            for (let x = 0; x < this.width; x++) {
                this.map[y][x] = { ...TerrainTypes.PLAIN, x, y };
            }
        }

        this.generateWater();
        this.generateMountains();
        this.generateForests();
        this.generateFortresses();
        this.generateRoads();
        this.calculateStartPositions();

        return {
            map: this.map,
            player1Start: this.player1Start,
            player2Start: this.player2Start
        };
    }

    seededRandom(seed) {
        return function() {
            seed = Math.sin(seed++) * 10000;
            return seed - Math.floor(seed);
        };
    }

    generateWater() {
        const waterCount = Math.floor(this.width * this.height * 0.08);
        
        for (let i = 0; i < waterCount; i++) {
            const centerX = Math.floor(Math.random() * this.width);
            const centerY = Math.floor(Math.random() * this.height);
            const size = Math.floor(Math.random() * 3) + 1;
            
            this.createBlob(centerX, centerY, size, TerrainTypes.WATER);
        }
        
        for (let y = 0; y < this.height; y++) {
            if (Math.random() < 0.3) {
                const x = Math.floor(Math.random() * this.width);
                this.map[y][x] = { ...TerrainTypes.WATER, x, y };
            }
        }
    }

    generateMountains() {
        const mountainCount = Math.floor(this.width * this.height * 0.1);
        
        for (let i = 0; i < mountainCount; i++) {
            const centerX = Math.floor(Math.random() * this.width);
            const centerY = Math.floor(Math.random() * this.height);
            const size = Math.floor(Math.random() * 2) + 1;
            
            for (let dy = -size; dy <= size; dy++) {
                for (let dx = -size; dx <= size; dx++) {
                    const x = centerX + dx;
                    const y = centerY + dy;
                    
                    if (this.isValidPosition(x, y) && this.map[y][x].id === 'plain') {
                        const distance = Math.abs(dx) + Math.abs(dy);
                        if (distance <= size) {
                            this.map[y][x] = { ...TerrainTypes.MOUNTAIN, x, y };
                        }
                    }
                }
            }
        }
    }

    generateForests() {
        const forestCount = Math.floor(this.width * this.height * 0.15);
        
        for (let i = 0; i < forestCount; i++) {
            const centerX = Math.floor(Math.random() * this.width);
            const centerY = Math.floor(Math.random() * this.height);
            const size = Math.floor(Math.random() * 3) + 2;
            
            this.createBlob(centerX, centerY, size, TerrainTypes.FOREST);
        }
    }

    generateFortresses() {
        const fortressCount = 2 + Math.floor(Math.random() * 2);
        const placed = [];
        
        for (let i = 0; i < fortressCount; i++) {
            let attempts = 0;
            while (attempts < 50) {
                const x = Math.floor(Math.random() * (this.width - 4)) + 2;
                const y = Math.floor(Math.random() * (this.height - 4)) + 2;
                
                const tooClose = placed.some(p => 
                    Math.abs(p.x - x) < 4 && Math.abs(p.y - y) < 4
                );
                
                if (!tooClose && this.map[y][x].id !== 'water') {
                    this.map[y][x] = { ...TerrainTypes.FORTRESS, x, y };
                    placed.push({ x, y });
                    break;
                }
                attempts++;
            }
        }
    }

    generateRoads() {
        const roads = Math.floor(Math.random() * 2) + 1;
        
        for (let i = 0; i < roads; i++) {
            let x = Math.floor(Math.random() * this.width);
            let y = 0;
            
            for (let j = 0; j < this.height; j++) {
                if (this.isValidPosition(x, y) && this.map[y][x].id === 'plain') {
                    this.map[y][x] = { ...TerrainTypes.ROAD, x, y };
                }
                
                if (Math.random() < 0.3) {
                    x += Math.random() < 0.5 ? 1 : -1;
                    x = Math.max(0, Math.min(this.width - 1, x));
                }
                y++;
            }
        }
    }

    createBlob(centerX, centerY, size, terrain) {
        for (let dy = -size; dy <= size; dy++) {
            for (let dx = -size; dx <= size; dx++) {
                const x = centerX + dx;
                const y = centerY + dy;
                
                if (this.isValidPosition(x, y) && this.map[y][x].id === 'plain') {
                    const distance = Math.sqrt(dx * dx + dy * dy);
                    const probability = 1 - (distance / (size + 1));
                    
                    if (Math.random() < probability) {
                        this.map[y][x] = { ...terrain, x, y };
                    }
                }
            }
        }
    }

    calculateStartPositions() {
        this.player1Start = [];
        this.player2Start = [];
        
        const startPositions1 = [
            { x: 1, y: 1 }, { x: 1, y: 2 }, { x: 0, y: 1 },
            { x: 2, y: 1 }, { x: 1, y: 0 }, { x: 0, y: 2 },
            { x: 2, y: 2 }, { x: 0, y: 0 }
        ];
        
        const startPositions2 = [
            { x: this.width - 2, y: this.height - 2 },
            { x: this.width - 2, y: this.height - 3 },
            { x: this.width - 1, y: this.height - 2 },
            { x: this.width - 3, y: this.height - 2 },
            { x: this.width - 2, y: this.height - 1 },
            { x: this.width - 1, y: this.height - 3 },
            { x: this.width - 3, y: this.height - 3 },
            { x: this.width - 1, y: this.height - 1 }
        ];
        
        for (const pos of startPositions1) {
            if (this.isValidPosition(pos.x, pos.y) && this.map[pos.y][pos.x].passable) {
                this.player1Start.push(pos);
            }
        }
        
        for (const pos of startPositions2) {
            if (this.isValidPosition(pos.x, pos.y) && this.map[pos.y][pos.x].passable) {
                this.player2Start.push(pos);
            }
        }
        
        this.ensurePathExists();
    }

    ensurePathExists() {
        let hasPath = false;
        let attempts = 0;
        
        while (!hasPath && attempts < 10) {
            hasPath = this.checkPathExists();
            
            if (!hasPath) {
                this.createPath();
            }
            attempts++;
        }
    }

    checkPathExists() {
        if (this.player1Start.length === 0 || this.player2Start.length === 0) {
            return false;
        }
        
        const start = this.player1Start[0];
        const end = this.player2Start[0];
        
        const visited = new Set();
        const queue = [start];
        
        while (queue.length > 0) {
            const current = queue.shift();
            const key = `${current.x},${current.y}`;
            
            if (current.x === end.x && current.y === end.y) {
                return true;
            }
            
            if (visited.has(key)) continue;
            visited.add(key);
            
            const directions = [
                { dx: 0, dy: -1 }, { dx: 0, dy: 1 },
                { dx: -1, dy: 0 }, { dx: 1, dy: 0 }
            ];
            
            for (const dir of directions) {
                const nx = current.x + dir.dx;
                const ny = current.y + dir.dy;
                
                if (this.isValidPosition(nx, ny) && this.map[ny][nx].passable) {
                    queue.push({ x: nx, y: ny });
                }
            }
        }
        
        return false;
    }

    createPath() {
        const start = this.player1Start[0] || { x: 1, y: 1 };
        const end = this.player2Start[0] || { x: this.width - 2, y: this.height - 2 };
        
        let current = { ...start };
        
        while (current.x !== end.x || current.y !== end.y) {
            if (this.map[current.y][current.x].id === 'water') {
                this.map[current.y][current.x] = { ...TerrainTypes.PLAIN, x: current.x, y: current.y };
            }
            
            if (current.x !== end.x) {
                current.x += current.x < end.x ? 1 : -1;
            } else if (current.y !== end.y) {
                current.y += current.y < end.y ? 1 : -1;
            }
            
            if (this.map[current.y][current.x].id === 'water') {
                this.map[current.y][current.x] = { ...TerrainTypes.PLAIN, x: current.x, y: current.y };
            }
        }
    }

    isValidPosition(x, y) {
        return x >= 0 && x < this.width && y >= 0 && y < this.height;
    }

    getTile(x, y) {
        if (this.isValidPosition(x, y)) {
            return this.map[y][x];
        }
        return null;
    }

    getMoveCost(x, y) {
        const tile = this.getTile(x, y);
        return tile ? tile.moveCost : Infinity;
    }

    getDefenseBonus(x, y) {
        const tile = this.getTile(x, y);
        return tile ? tile.defBonus : 0;
    }

    isPassable(x, y) {
        const tile = this.getTile(x, y);
        return tile ? tile.passable : false;
    }

    calculateReachable(unit, occupiedTiles) {
        const reachable = [];
        const moveRange = unit.move;
        const visited = new Map();
        const queue = [{ x: unit.x, y: unit.y, cost: 0 }];
        visited.set(`${unit.x},${unit.y}`, 0);

        while (queue.length > 0) {
            const current = queue.shift();
            
            if (current.cost > 0) {
                const isOccupied = occupiedTiles.some(t => 
                    t.x === current.x && t.y === current.y && 
                    !(t.x === unit.x && t.y === unit.y)
                );
                
                if (!isOccupied) {
                    reachable.push({ x: current.x, y: current.y, cost: current.cost });
                }
            }

            const directions = [
                { dx: 0, dy: -1 }, { dx: 0, dy: 1 },
                { dx: -1, dy: 0 }, { dx: 1, dy: 0 }
            ];

            for (const dir of directions) {
                const nx = current.x + dir.dx;
                const ny = current.y + dir.dy;
                const key = `${nx},${ny}`;

                if (this.isValidPosition(nx, ny) && this.isPassable(nx, ny)) {
                    const moveCost = this.getMoveCost(nx, ny);
                    const newCost = current.cost + moveCost;

                    if (newCost <= moveRange && (!visited.has(key) || visited.get(key) > newCost)) {
                        visited.set(key, newCost);
                        queue.push({ x: nx, y: ny, cost: newCost });
                    }
                }
            }
        }

        return reachable;
    }

    calculateAttackRange(unit) {
        const attackTiles = [];
        const range = unit.range;

        for (let dy = -range; dy <= range; dy++) {
            for (let dx = -range; dx <= range; dx++) {
                const distance = Math.abs(dx) + Math.abs(dy);
                if (distance > 0 && distance <= range) {
                    const x = unit.x + dx;
                    const y = unit.y + dy;
                    
                    if (this.isValidPosition(x, y)) {
                        attackTiles.push({ x, y, distance });
                    }
                }
            }
        }

        return attackTiles;
    }

    getDistance(x1, y1, x2, y2) {
        return Math.abs(x1 - x2) + Math.abs(y1 - y2);
    }
}
