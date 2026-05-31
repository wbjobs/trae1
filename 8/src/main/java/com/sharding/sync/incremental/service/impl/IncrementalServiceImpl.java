package com.sharding.sync.incremental.service.impl;

import com.baomidou.mybatisplus.core.conditions.query.LambdaQueryWrapper;
import com.baomidou.mybatisplus.extension.service.impl.ServiceImpl;
import com.sharding.sync.incremental.entity.BinlogPosition;
import com.sharding.sync.incremental.entity.IncrementalEvent;
import com.sharding.sync.incremental.mapper.BinlogPositionMapper;
import com.sharding.sync.incremental.mapper.IncrementalEventMapper;
import com.sharding.sync.incremental.service.IncrementalService;
import lombok.RequiredArgsConstructor;
import org.springframework.stereotype.Service;

import java.time.LocalDateTime;
import java.util.List;

@Service
@RequiredArgsConstructor
public class IncrementalServiceImpl implements IncrementalService {

    private final BinlogPositionMapper binlogPositionMapper;
    private final IncrementalEventMapper incrementalEventMapper;

    @Override
    public BinlogPosition getPosition(String logicTable) {
        return binlogPositionMapper.selectOne(
                new LambdaQueryWrapper<BinlogPosition>().eq(BinlogPosition::getLogicTable, logicTable));
    }

    @Override
    public void savePosition(BinlogPosition position) {
        position.setUpdateTime(LocalDateTime.now());
        BinlogPosition exist = getPosition(position.getLogicTable());
        if (exist == null) {
            binlogPositionMapper.insert(position);
        } else {
            position.setId(exist.getId());
            binlogPositionMapper.updateById(position);
        }
    }

    @Override
    public void saveEvent(IncrementalEvent event) {
        event.setCreateTime(LocalDateTime.now());
        event.setUpdateTime(LocalDateTime.now());
        if (event.getStatus() == null) {
            event.setStatus(0);
        }
        if (event.getRetryCount() == null) {
            event.setRetryCount(0);
        }
        incrementalEventMapper.insert(event);
    }

    @Override
    public List<IncrementalEvent> listPendingEvents(String logicTable, int limit) {
        LambdaQueryWrapper<IncrementalEvent> wrapper = new LambdaQueryWrapper<>();
        wrapper.eq(IncrementalEvent::getLogicTable, logicTable)
                .in(IncrementalEvent::getStatus, 0, 2)
                .lt(IncrementalEvent::getRetryCount, 5)
                .orderByAsc(IncrementalEvent::getId)
                .last("limit " + limit);
        return incrementalEventMapper.selectList(wrapper);
    }

    @Override
    public void markEventSuccess(Long eventId) {
        IncrementalEvent event = incrementalEventMapper.selectById(eventId);
        if (event == null) {
            return;
        }
        event.setStatus(1);
        event.setUpdateTime(LocalDateTime.now());
        incrementalEventMapper.updateById(event);
    }

    @Override
    public void markEventFailed(Long eventId, String errorMsg) {
        IncrementalEvent event = incrementalEventMapper.selectById(eventId);
        if (event == null) {
            return;
        }
        event.setStatus(2);
        event.setRetryCount(event.getRetryCount() + 1);
        event.setErrorMsg(errorMsg);
        event.setUpdateTime(LocalDateTime.now());
        incrementalEventMapper.updateById(event);
    }

    @Override
    public List<IncrementalEvent> listRecent(String logicTable, int limit) {
        LambdaQueryWrapper<IncrementalEvent> wrapper = new LambdaQueryWrapper<>();
        wrapper.eq(IncrementalEvent::getLogicTable, logicTable)
                .orderByDesc(IncrementalEvent::getId)
                .last("limit " + limit);
        return incrementalEventMapper.selectList(wrapper);
    }
}
