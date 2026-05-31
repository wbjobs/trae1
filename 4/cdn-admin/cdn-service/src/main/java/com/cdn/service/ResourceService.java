package com.cdn.service;

import com.baomidou.mybatisplus.core.conditions.query.LambdaQueryWrapper;
import com.baomidou.mybatisplus.extension.plugins.pagination.Page;
import com.cdn.domain.dto.CdnResourceDTO;
import com.cdn.domain.entity.CdnResource;
import com.cdn.domain.mapper.CdnResourceMapper;
import lombok.RequiredArgsConstructor;
import org.springframework.beans.BeanUtils;
import org.springframework.stereotype.Service;

import java.time.LocalDateTime;
import java.util.List;

@Service
@RequiredArgsConstructor
public class ResourceService {

    private final CdnResourceMapper resourceMapper;

    public Page<CdnResource> page(int pageNum, int pageSize, String keyword) {
        Page<CdnResource> page = new Page<>(pageNum, pageSize);
        LambdaQueryWrapper<CdnResource> qw = new LambdaQueryWrapper<>();
        if (keyword != null && !keyword.isEmpty()) {
            qw.like(CdnResource::getResourceUrl, keyword)
                    .or().like(CdnResource::getResourceName, keyword);
        }
        qw.orderByDesc(CdnResource::getCreateTime);
        return resourceMapper.selectPage(page, qw);
    }

    public CdnResource getById(Long id) {
        return resourceMapper.selectById(id);
    }

    public CdnResource save(CdnResourceDTO dto) {
        CdnResource r = new CdnResource();
        BeanUtils.copyProperties(dto, r);
        if (r.getId() == null) {
            r.setCreateTime(LocalDateTime.now());
            r.setUpdateTime(LocalDateTime.now());
            if (r.getStatus() == null) r.setStatus(1);
            resourceMapper.insert(r);
        } else {
            r.setUpdateTime(LocalDateTime.now());
            resourceMapper.updateById(r);
        }
        return r;
    }

    public void delete(Long id) {
        resourceMapper.deleteById(id);
    }

    public List<CdnResource> listAll() {
        return resourceMapper.selectList(null);
    }
}
