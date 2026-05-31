package com.cdn.service;

import com.baomidou.mybatisplus.core.conditions.query.LambdaQueryWrapper;
import com.baomidou.mybatisplus.extension.plugins.pagination.Page;
import com.cdn.domain.dto.CacheConfigDTO;
import com.cdn.domain.entity.CacheConfig;
import com.cdn.domain.mapper.CacheConfigMapper;
import lombok.RequiredArgsConstructor;
import org.springframework.beans.BeanUtils;
import org.springframework.stereotype.Service;

import java.time.LocalDateTime;
import java.util.List;

@Service
@RequiredArgsConstructor
public class CacheConfigService {

    private final CacheConfigMapper configMapper;

    public Page<CacheConfig> page(int pageNum, int pageSize, String keyword) {
        Page<CacheConfig> page = new Page<>(pageNum, pageSize);
        LambdaQueryWrapper<CacheConfig> qw = new LambdaQueryWrapper<>();
        if (keyword != null && !keyword.isEmpty()) {
            qw.like(CacheConfig::getConfigKey, keyword)
                    .or().like(CacheConfig::getResourcePattern, keyword);
        }
        qw.orderByDesc(CacheConfig::getCreateTime);
        return configMapper.selectPage(page, qw);
    }

    public CacheConfig save(CacheConfigDTO dto) {
        CacheConfig c = new CacheConfig();
        BeanUtils.copyProperties(dto, c);
        if (c.getId() == null) {
            c.setCreateTime(LocalDateTime.now());
            c.setUpdateTime(LocalDateTime.now());
            if (c.getStatus() == null) c.setStatus(1);
            if (c.getTtlSeconds() == null) c.setTtlSeconds(3600);
            if (c.getCacheStrategy() == null) c.setCacheStrategy(1);
            configMapper.insert(c);
        } else {
            c.setUpdateTime(LocalDateTime.now());
            configMapper.updateById(c);
        }
        return c;
    }

    public void delete(Long id) {
        configMapper.deleteById(id);
    }

    public List<CacheConfig> listAll() {
        return configMapper.selectList(null);
    }
}
