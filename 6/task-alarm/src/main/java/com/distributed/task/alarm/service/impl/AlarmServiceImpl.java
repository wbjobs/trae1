package com.distributed.task.alarm.service.impl;

import cn.hutool.core.util.IdUtil;
import cn.hutool.core.util.StrUtil;
import com.distributed.task.common.dto.AlarmDTO;
import com.distributed.task.common.entity.AlarmRecord;
import com.distributed.task.alarm.mapper.AlarmRecordMapper;
import com.distributed.task.alarm.service.AlarmService;
import lombok.RequiredArgsConstructor;
import lombok.extern.slf4j.Slf4j;
import org.springframework.beans.factory.annotation.Value;
import org.springframework.stereotype.Service;

import java.time.LocalDateTime;

@Slf4j
@Service
@RequiredArgsConstructor
public class AlarmServiceImpl implements AlarmService {

    private final AlarmRecordMapper alarmRecordMapper;

    @Value("${task.alarm.default-receiver:admin@example.com}")
    private String defaultReceiver;

    @Override
    public boolean send(AlarmDTO dto) {
        AlarmRecord record = new AlarmRecord();
        record.setAlarmNo("ALM_" + System.currentTimeMillis() + "_" + IdUtil.fastSimpleUUID().substring(0, 6));
        record.setTaskNo(dto.getTaskNo());
        record.setAlarmType(dto.getAlarmType());
        record.setAlarmLevel(dto.getAlarmLevel() != null ? dto.getAlarmLevel() : 3);
        record.setAlarmContent(StrUtil.maxLength(dto.getAlarmContent(), 2000));
        record.setReceiver(StrUtil.isNotBlank(dto.getReceiver()) ? dto.getReceiver() : defaultReceiver);
        record.setCreateTime(LocalDateTime.now());
        try {
            doSend(record);
            record.setSendStatus(1);
            log.info("告警发送成功 alarmNo={} type={}", record.getAlarmNo(), record.getAlarmType());
        } catch (Exception e) {
            record.setSendStatus(0);
            record.setFailReason(StrUtil.maxLength(e.getMessage(), 500));
            log.error("告警发送失败 alarmNo={}", record.getAlarmNo(), e);
        }
        alarmRecordMapper.insert(record);
        return Integer.valueOf(1).equals(record.getSendStatus());
    }

    private void doSend(AlarmRecord record) {
        log.warn("[ALERT] type={} level={} taskNo={} receiver={} content={}",
                record.getAlarmType(), record.getAlarmLevel(),
                record.getTaskNo(), record.getReceiver(), record.getAlarmContent());
    }
}
