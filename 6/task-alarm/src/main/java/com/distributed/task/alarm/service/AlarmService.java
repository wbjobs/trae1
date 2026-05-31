package com.distributed.task.alarm.service;

import com.distributed.task.common.dto.AlarmDTO;

public interface AlarmService {

    boolean send(AlarmDTO dto);
}
