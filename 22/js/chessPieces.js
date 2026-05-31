const UnitTypes = {
    WARRIOR: 'warrior',
    ARCHER: 'archer',
    MAGE: 'mage',
    KNIGHT: 'knight',
    HEALER: 'healer',
    ASSASSIN: 'assassin'
};

const Skills = {
    SLASH: {
        id: 'slash',
        name: '斩击',
        description: '对目标造成150%攻击伤害',
        damageMultiplier: 1.5,
        range: 1,
        cooldown: 0,
        type: 'damage',
        effect: null
    },
    POWER_STRIKE: {
        id: 'power_strike',
        name: '强力一击',
        description: '对目标造成200%攻击伤害，降低目标防御',
        damageMultiplier: 2.0,
        range: 1,
        cooldown: 2,
        type: 'damage',
        effect: { defReduction: 0.3, duration: 2 }
    },
    ARROW_SHOT: {
        id: 'arrow_shot',
        name: '箭雨',
        description: '对目标造成120%攻击伤害',
        damageMultiplier: 1.2,
        range: 4,
        cooldown: 0,
        type: 'damage',
        effect: null
    },
    PIERCING_ARROW: {
        id: 'piercing_arrow',
        name: '穿透箭',
        description: '对目标造成180%攻击伤害，无视部分防御',
        damageMultiplier: 1.8,
        range: 5,
        cooldown: 2,
        type: 'damage',
        effect: { ignoreDefense: 0.5 }
    },
    FIREBALL: {
        id: 'fireball',
        name: '火球术',
        description: '对目标造成160%攻击魔法伤害',
        damageMultiplier: 1.6,
        range: 3,
        cooldown: 0,
        type: 'magic',
        effect: null
    },
    LIGHTNING: {
        id: 'lightning',
        name: '闪电链',
        description: '对目标及相邻敌人造成140%攻击魔法伤害',
        damageMultiplier: 1.4,
        range: 3,
        cooldown: 2,
        type: 'magic',
        effect: { chain: true, chainCount: 2 }
    },
    CHARGE: {
        id: 'charge',
        name: '冲锋',
        description: '向目标移动并造成130%攻击伤害',
        damageMultiplier: 1.3,
        range: 2,
        cooldown: 1,
        type: 'damage',
        effect: { moveToTarget: true }
    },
    SHIELD_BASH: {
        id: 'shield_bash',
        name: '盾击',
        description: '造成100%攻击伤害并使目标眩晕',
        damageMultiplier: 1.0,
        range: 1,
        cooldown: 3,
        type: 'damage',
        effect: { stun: 1 }
    },
    HEAL: {
        id: 'heal',
        name: '治疗术',
        description: '恢复目标150%攻击生命值',
        damageMultiplier: 1.5,
        range: 2,
        cooldown: 1,
        type: 'heal',
        effect: null
    },
    HOLY_LIGHT: {
        id: 'holy_light',
        name: '神圣之光',
        description: '恢复范围内所有友军200%攻击生命值',
        damageMultiplier: 2.0,
        range: 2,
        cooldown: 3,
        type: 'heal',
        effect: { aoe: true }
    },
    SHADOW_STRIKE: {
        id: 'shadow_strike',
        name: '暗影突袭',
        description: '瞬移到目标身边造成170%攻击伤害',
        damageMultiplier: 1.7,
        range: 3,
        cooldown: 2,
        type: 'damage',
        effect: { teleport: true }
    },
    POISON_DAGGER: {
        id: 'poison_dagger',
        name: '毒刃',
        description: '造成120%攻击伤害并使目标中毒',
        damageMultiplier: 1.2,
        range: 1,
        cooldown: 1,
        type: 'damage',
        effect: { poison: { damage: 0.1, duration: 3 } }
    }
};

const UnitTemplates = {
    [UnitTypes.WARRIOR]: {
        name: '战士',
        type: UnitTypes.WARRIOR,
        baseHp: 120,
        baseAtk: 25,
        baseDef: 15,
        move: 3,
        range: 1,
        skills: ['slash', 'power_strike'],
        counters: [UnitTypes.ARCHER],
        weakTo: [UnitTypes.MAGE],
        color: '#ff6b6b'
    },
    [UnitTypes.ARCHER]: {
        name: '弓箭手',
        type: UnitTypes.ARCHER,
        baseHp: 80,
        baseAtk: 30,
        baseDef: 8,
        move: 4,
        range: 4,
        skills: ['arrow_shot', 'piercing_arrow'],
        counters: [UnitTypes.MAGE],
        weakTo: [UnitTypes.WARRIOR, UnitTypes.KNIGHT],
        color: '#4ecdc4'
    },
    [UnitTypes.MAGE]: {
        name: '法师',
        type: UnitTypes.MAGE,
        baseHp: 70,
        baseAtk: 35,
        baseDef: 5,
        move: 3,
        range: 3,
        skills: ['fireball', 'lightning'],
        counters: [UnitTypes.KNIGHT, UnitTypes.WARRIOR],
        weakTo: [UnitTypes.ARCHER, UnitTypes.ASSASSIN],
        color: '#a855f7'
    },
    [UnitTypes.KNIGHT]: {
        name: '骑士',
        type: UnitTypes.KNIGHT,
        baseHp: 150,
        baseAtk: 22,
        baseDef: 20,
        move: 2,
        range: 1,
        skills: ['charge', 'shield_bash'],
        counters: [UnitTypes.ARCHER],
        weakTo: [UnitTypes.MAGE],
        color: '#fbbf24'
    },
    [UnitTypes.HEALER]: {
        name: '治疗师',
        type: UnitTypes.HEALER,
        baseHp: 90,
        baseAtk: 15,
        baseDef: 10,
        move: 3,
        range: 2,
        skills: ['heal', 'holy_light'],
        counters: [],
        weakTo: [UnitTypes.ASSASSIN, UnitTypes.WARRIOR],
        color: '#10b981'
    },
    [UnitTypes.ASSASSIN]: {
        name: '刺客',
        type: UnitTypes.ASSASSIN,
        baseHp: 75,
        baseAtk: 32,
        baseDef: 6,
        move: 5,
        range: 1,
        skills: ['shadow_strike', 'poison_dagger'],
        counters: [UnitTypes.MAGE, UnitTypes.HEALER],
        weakTo: [UnitTypes.KNIGHT],
        color: '#6b7280'
    }
};

const UnitRarities = {
    COMMON: { id: 'common', name: '普通', multiplier: 1.0, color: '#9ca3af' },
    UNCOMMON: { id: 'uncommon', name: '优秀', multiplier: 1.1, color: '#22c55e' },
    RARE: { id: 'rare', name: '稀有', multiplier: 1.25, color: '#3b82f6' },
    EPIC: { id: 'epic', name: '史诗', multiplier: 1.5, color: '#a855f7' },
    LEGENDARY: { id: 'legendary', name: '传说', multiplier: 2.0, color: '#f59e0b' }
};

class UnitRandomizer {
    static randomRarity() {
        const rand = Math.random();
        if (rand < 0.5) return UnitRarities.COMMON;
        if (rand < 0.8) return UnitRarities.UNCOMMON;
        if (rand < 0.95) return UnitRarities.RARE;
        if (rand < 0.99) return UnitRarities.EPIC;
        return UnitRarities.LEGENDARY;
    }

    static randomizeUnit(unit, forceRarity = null) {
        const rarity = forceRarity || this.randomRarity();
        unit.rarity = rarity;
        
        const nameSuffixes = {
            common: ['新兵', '见习', '学徒'],
            uncommon: ['精锐', '熟练', '资深'],
            rare: ['精英', '专家', '大师'],
            epic: ['传奇', '史诗', '英雄'],
            legendary: ['神话', '超凡', '至高']
        };
        
        const suffix = nameSuffixes[rarity.id][Math.floor(Math.random() * nameSuffixes[rarity.id].length)];
        unit.name = `${suffix}${unit.name}`;
        
        const mult = rarity.multiplier;
        const hpVariance = 0.85 + Math.random() * 0.3;
        const atkVariance = 0.85 + Math.random() * 0.3;
        const defVariance = 0.85 + Math.random() * 0.3;
        
        unit.maxHp = Math.floor(unit.maxHp * mult * hpVariance);
        unit.hp = unit.maxHp;
        unit.baseAtk = Math.floor(unit.baseAtk * mult * atkVariance);
        unit.atk = unit.baseAtk;
        unit.baseDef = Math.floor(unit.baseDef * mult * defVariance);
        unit.def = unit.baseDef;
        
        const traits = this.generateTraits(unit.type, rarity);
        unit.traits = traits;
        
        return unit;
    }

    static generateTraits(unitType, rarity) {
        const traits = [];
        const possibleTraits = {
            warrior: [
                { id: 'berserker', name: '狂暴', effect: 'hp<30%时攻击+30%' },
                { id: 'tough', name: '坚韧', effect: '生命+15%' },
                { id: 'cleave', name: '横扫', effect: '攻击有20%几率伤害相邻敌人' }
            ],
            archer: [
                { id: 'eagle_eye', name: '鹰眼', effect: '射程+1' },
                { id: 'rapid_fire', name: '速射', effect: '技能冷却-1' },
                { id: 'sniper', name: '狙击', effect: '暴击率+20%' }
            ],
            mage: [
                { id: 'arcane_mastery', name: '奥术精通', effect: '魔法伤害+20%' },
                { id: 'mana_surge', name: '魔力涌动', effect: '每回合恢复5%生命' },
                { id: 'elemental', name: '元素亲和', effect: '技能范围+1' }
            ],
            knight: [
                { id: 'shield_wall', name: '盾墙', effect: '防御+25%' },
                { id: 'charge_master', name: '冲锋大师', effect: '冲锋技能伤害+30%' },
                { id: 'aura', name: '守护光环', effect: '相邻友军防御+10%' }
            ],
            healer: [
                { id: 'divine_touch', name: '神圣之触', effect: '治疗量+25%' },
                { id: 'life_steal', name: '生命汲取', effect: '治疗时自身恢复10%生命' },
                { id: 'purify', name: '净化', effect: '治疗时有几率移除负面效果' }
            ],
            assassin: [
                { id: 'shadow_step', name: '暗影步', effect: '移动+1' },
                { id: 'critical', name: '致命', effect: '暴击率+30%' },
                { id: 'poison_master', name: '剧毒精通', effect: '中毒伤害+50%' }
            ]
        };
        
        const typeTraits = possibleTraits[unitType] || [];
        const maxTraits = rarity.id === 'legendary' ? 2 : rarity.id === 'epic' ? 2 : rarity.id === 'rare' ? 1 : 0;
        
        for (let i = 0; i < maxTraits && typeTraits.length > 0; i++) {
            const index = Math.floor(Math.random() * typeTraits.length);
            traits.push(typeTraits.splice(index, 1)[0]);
        }
        
        return traits;
    }

    static applyTraitEffects(unit) {
        if (!unit.traits) return;
        
        for (const trait of unit.traits) {
            switch (trait.id) {
                case 'tough':
                    unit.maxHp = Math.floor(unit.maxHp * 1.15);
                    unit.hp = unit.maxHp;
                    break;
                case 'eagle_eye':
                case 'elemental':
                    unit.range += 1;
                    break;
                case 'shield_wall':
                    unit.baseDef = Math.floor(unit.baseDef * 1.25);
                    unit.def = unit.baseDef;
                    break;
                case 'shadow_step':
                    unit.move += 1;
                    break;
            }
        }
    }
}

class Unit {
    constructor(template, player, x, y, level = 1) {
        this.id = `${player}_${template.type}_${Date.now()}_${Math.random()}`;
        this.name = template.name;
        this.type = template.type;
        this.player = player;
        this.x = x;
        this.y = y;
        this.level = level;
        this.rarity = UnitRarities.COMMON;
        this.traits = [];
        this.maxHp = Math.floor(template.baseHp * (1 + (level - 1) * 0.2));
        this.hp = this.maxHp;
        this.baseAtk = Math.floor(template.baseAtk * (1 + (level - 1) * 0.15));
        this.atk = this.baseAtk;
        this.baseDef = Math.floor(template.baseDef * (1 + (level - 1) * 0.1));
        this.def = this.baseDef;
        this.move = template.move;
        this.range = template.range;
        this.skills = template.skills.map(skillId => ({
            ...Skills[skillId.toUpperCase()],
            currentCooldown: 0
        }));
        this.counters = template.counters;
        this.weakTo = template.weakTo;
        this.color = template.color;
        this.hasMoved = false;
        this.hasActed = false;
        this.buffs = [];
        this.debuffs = [];
        this.statusEffects = [];
        this.critChance = 0.1;
        this.critDamage = 1.5;
    }

    takeDamage(damage, isMagic = false, ignoreDefense = 0) {
        let effectiveDef = isMagic ? this.def * 0.5 : this.def;
        effectiveDef *= (1 - ignoreDefense);
        let actualDamage = Math.max(1, Math.floor(damage - effectiveDef * 0.5));
        
        if (Math.random() < this.critChance) {
            actualDamage = Math.floor(actualDamage * this.critDamage);
            this.lastHitCrit = true;
        } else {
            this.lastHitCrit = false;
        }
        
        this.hp = Math.max(0, this.hp - actualDamage);
        return actualDamage;
    }

    heal(amount) {
        const actualHeal = Math.min(amount, this.maxHp - this.hp);
        this.hp += actualHeal;
        return actualHeal;
    }

    addStatusEffect(effect) {
        this.statusEffects.push({
            ...effect,
            remainingDuration: effect.duration
        });
        if (effect.type === 'poison') {
            this.debuffs.push(effect);
        }
    }

    processTurnEffects() {
        let damage = 0;
        let healAmount = 0;
        
        if (this.traits.some(t => t.id === 'mana_surge')) {
            healAmount += Math.floor(this.maxHp * 0.05);
        }
        
        this.statusEffects = this.statusEffects.filter(effect => {
            if (effect.type === 'poison') {
                damage += Math.floor(this.maxHp * effect.damage);
            }
            effect.remainingDuration--;
            return effect.remainingDuration > 0;
        });
        
        if (damage > 0) {
            this.hp = Math.max(0, this.hp - damage);
        }
        if (healAmount > 0) {
            this.hp = Math.min(this.maxHp, this.hp + healAmount);
        }
        
        return { damage, heal: healAmount };
    }

    isStunned() {
        return this.statusEffects.some(e => e.stun);
    }

    getSkill(skillId) {
        return this.skills.find(s => s.id === skillId);
    }

    useSkill(skillId) {
        const skill = this.getSkill(skillId);
        if (skill && skill.currentCooldown <= 0) {
            let cooldown = skill.cooldown;
            if (this.traits.some(t => t.id === 'rapid_fire')) {
                cooldown = Math.max(0, cooldown - 1);
            }
            skill.currentCooldown = cooldown;
            return true;
        }
        return false;
    }

    reduceCooldowns() {
        this.skills.forEach(skill => {
            if (skill.currentCooldown > 0) {
                skill.currentCooldown--;
            }
        });
    }

    resetTurn() {
        this.hasMoved = false;
        this.hasActed = false;
        this.reduceCooldowns();
    }

    getCounterBonus(targetType) {
        if (this.counters.includes(targetType)) {
            return 1.3;
        }
        return 1.0;
    }

    getWeaknessPenalty(attackerType) {
        if (this.weakTo.includes(attackerType)) {
            return 0.7;
        }
        return 1.0;
    }

    getEffectiveAtk() {
        let atk = this.baseAtk;
        
        if (this.hp < this.maxHp * 0.3 && this.traits.some(t => t.id === 'berserker')) {
            atk = Math.floor(atk * 1.3);
        }
        
        return atk;
    }

    getEffectiveDef() {
        return this.baseDef;
    }
}

const GameConfig = {
    mapWidth: 15,
    mapHeight: 12,
    tileSize: 50,
    unitsPerPlayer: 5,
    maxLevel: 5,
    experiencePerKill: 100,
    enableRandomAttributes: true
};
