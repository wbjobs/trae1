package com.sharding.sync.shard.service.impl;

import com.baomidou.mybatisplus.core.conditions.query.LambdaQueryWrapper;
import com.baomidou.mybatisplus.core.metadata.IPage;
import com.baomidou.mybatisplus.extension.plugins.pagination.Page;
import com.baomidou.mybatisplus.extension.service.impl.ServiceImpl;
import com.sharding.sync.common.BusinessException;
import com.sharding.sync.common.ResultCode;
import com.sharding.sync.config.MyCatProperties;
import com.sharding.sync.shard.dto.ShardRuleDTO;
import com.sharding.sync.shard.entity.ShardRule;
import com.sharding.sync.shard.mapper.ShardRuleMapper;
import com.sharding.sync.shard.service.ShardRuleService;
import lombok.RequiredArgsConstructor;
import lombok.extern.slf4j.Slf4j;
import org.springframework.context.ApplicationEventPublisher;
import org.springframework.stereotype.Service;
import org.springframework.util.CollectionUtils;
import org.springframework.util.StringUtils;

import javax.annotation.PostConstruct;
import java.time.LocalDateTime;
import java.util.List;
import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;
import java.util.stream.Collectors;

@Slf4j
@Service
@RequiredArgsConstructor
public class ShardRuleServiceImpl extends ServiceImpl<ShardRuleMapper, ShardRule> implements ShardRuleService {

    private final MyCatProperties myCatProperties;
    private final ApplicationEventPublisher eventPublisher;

    private final Map<String, ShardRule> ruleCache = new ConcurrentHashMap<>();

    @PostConstruct
    public void init() {
        loadFromDb();
        loadFromProperties();
        log.info("分片规则加载完成, 共 {} 条", ruleCache.size());
    }

    private void loadFromDb() {
        try {
            List<ShardRule> list = list();
            list.forEach(r -> ruleCache.put(r.getLogicTable(), r));
        } catch (Exception e) {
            log.warn("从数据库加载分片规则失败: {}", e.getMessage());
        }
    }

    private void loadFromProperties() {
        if (CollectionUtils.isEmpty(myCatProperties.getTables())) {
            return;
        }
        for (MyCatProperties.LogicTable t : myCatProperties.getTables()) {
            if (ruleCache.containsKey(t.getName())) {
                continue;
            }
            ShardRule rule = new ShardRule();
            rule.setLogicTable(t.getName());
            rule.setShardingColumn(t.getShardingColumn());
            rule.setAlgorithm(t.getAlgorithm());
            rule.setShardCount(t.getShardCount());
            rule.setPrimaryKey(t.getPrimaryKey());
            rule.setShardNodes(String.join(",", t.getShardNodes()));
            rule.setStatus(1);
            ruleCache.put(t.getName(), rule);
        }
    }

    @Override
    public ShardRule save(ShardRuleDTO dto) {
        ShardRule exist = getByLogicTable(dto.getLogicTable());
        if (exist != null) {
            throw new BusinessException("逻辑表分片规则已存在: " + dto.getLogicTable());
        }
        ShardRule rule = convert(dto);
        rule.setCreateTime(LocalDateTime.now());
        rule.setUpdateTime(LocalDateTime.now());
        save(rule);
        ruleCache.put(rule.getLogicTable(), rule);
        publishChangeEvent(null, rule);
        return rule;
    }

    @Override
    public ShardRule update(ShardRuleDTO dto) {
        if (dto.getId() == null) {
            throw new BusinessException(ResultCode.PARAM_ERROR.getCode(), "id 不能为空");
        }
        ShardRule oldRule = getById(dto.getId());
        if (oldRule == null) {
            throw new BusinessException(ResultCode.SHARD_RULE_NOT_FOUND);
        }
        ShardRule updated = convert(dto);
        updated.setId(oldRule.getId());
        updated.setCreateTime(oldRule.getCreateTime());
        updated.setUpdateTime(LocalDateTime.now());
        updateById(updated);

        if (!oldRule.getLogicTable().equals(updated.getLogicTable())) {
            ruleCache.remove(oldRule.getLogicTable());
            log.info("分片规则逻辑表变更: {} -> {}", oldRule.getLogicTable(), updated.getLogicTable());
        }
        ruleCache.put(updated.getLogicTable(), updated);

        boolean keyChanged = !oldRule.getShardingColumn().equals(updated.getShardingColumn())
                || !oldRule.getAlgorithm().equals(updated.getAlgorithm())
                || !oldRule.getShardCount().equals(updated.getShardCount());
        if (keyChanged) {
            log.warn("分片规则关键字段变更 table={}, 旧: col={} algo={} cnt={}, 新: col={} algo={} cnt={}, 需手动触发数据重分布",
                    updated.getLogicTable(),
                    oldRule.getShardingColumn(), oldRule.getAlgorithm(), oldRule.getShardCount(),
                    updated.getShardingColumn(), updated.getAlgorithm(), updated.getShardCount());
        }

        publishChangeEvent(oldRule, updated);
        return updated;
    }

    @Override
    public void delete(Long id) {
        ShardRule rule = getById(id);
        if (rule == null) {
            throw new BusinessException(ResultCode.SHARD_RULE_NOT_FOUND);
        }
        removeById(id);
        ruleCache.remove(rule.getLogicTable());
        publishChangeEvent(rule, null);
    }

    @Override
    public ShardRule getById(Long id) {
        return getBaseMapper().selectById(id);
    }

    @Override
    public ShardRule getByLogicTable(String logicTable) {
        ShardRule cached = ruleCache.get(logicTable);
        if (cached != null) {
            return cached;
        }
        ShardRule db = getOne(new LambdaQueryWrapper<ShardRule>().eq(ShardRule::getLogicTable, logicTable));
        if (db != null) {
            ruleCache.put(logicTable, db);
        }
        return db;
    }

    @Override
    public List<ShardRule> listAll() {
        return ruleCache.values().stream().collect(Collectors.toList());
    }

    @Override
    public IPage<ShardRule> page(Page<ShardRule> page, String keyword) {
        LambdaQueryWrapper<ShardRule> wrapper = new LambdaQueryWrapper<>();
        if (StringUtils.hasText(keyword)) {
            wrapper.like(ShardRule::getLogicTable, keyword);
        }
        wrapper.orderByDesc(ShardRule::getUpdateTime);
        return page(page, wrapper);
    }

    @Override
    public void refreshCache() {
        ruleCache.clear();
        loadFromDb();
        loadFromProperties();
        log.info("分片规则缓存已刷新, 共 {} 条", ruleCache.size());
    }

    private void publishChangeEvent(ShardRule oldRule, ShardRule newRule) {
        try {
            eventPublisher.publishEvent(new ShardRuleChangeEvent(oldRule, newRule));
        } catch (Exception e) {
            log.warn("发布分片规则变更事件失败: {}", e.getMessage());
        }
    }

    private ShardRule convert(ShardRuleDTO dto) {
        ShardRule rule = new ShardRule();
        rule.setLogicTable(dto.getLogicTable());
        rule.setShardingColumn(dto.getShardingColumn());
        rule.setAlgorithm(dto.getAlgorithm());
        rule.setShardCount(dto.getShardCount());
        rule.setPrimaryKey(dto.getPrimaryKey());
        if (!CollectionUtils.isEmpty(dto.getShardNodes())) {
            rule.setShardNodes(String.join(",", dto.getShardNodes()));
        }
        rule.setStatus(dto.getStatus());
        rule.setRemark(dto.getRemark());
        return rule;
    }

    @lombok.Getter
    @lombok.AllArgsConstructor
    public static class ShardRuleChangeEvent {
        private final ShardRule oldRule;
        private final ShardRule newRule;
    }
}
