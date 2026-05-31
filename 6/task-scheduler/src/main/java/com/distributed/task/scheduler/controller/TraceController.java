package com.distributed.task.scheduler.controller;

import cn.hutool.core.bean.BeanUtil;
import com.baomidou.mybatisplus.core.conditions.query.LambdaQueryWrapper;
import com.distributed.task.common.dto.TaskTraceVO;
import com.distributed.task.common.dto.TaskVO;
import com.distributed.task.common.entity.AlarmRecord;
import com.distributed.task.common.entity.TaskInfo;
import com.distributed.task.common.entity.TaskLog;
import com.distributed.task.common.enums.TaskStatus;
import com.distributed.task.common.feign.AlarmQueryClient;
import com.distributed.task.common.feign.TaskLogQueryClient;
import com.distributed.task.common.result.R;
import com.distributed.task.scheduler.mapper.TaskInfoMapper;
import io.swagger.annotations.Api;
import io.swagger.annotations.ApiImplicitParam;
import io.swagger.annotations.ApiOperation;
import lombok.RequiredArgsConstructor;
import lombok.extern.slf4j.Slf4j;
import org.springframework.web.bind.annotation.*;

import java.util.Collections;
import java.util.List;

@Api(tags = "任务链路追踪")
@RestController
@RequestMapping("/task")
@RequiredArgsConstructor
@Slf4j
public class TraceController {

    private final TaskInfoMapper taskInfoMapper;
    private final TaskLogQueryClient taskLogQueryClient;
    private final AlarmQueryClient alarmQueryClient;

    @ApiOperation("查询任务完整链路(基础信息+日志+告警)")
    @ApiImplicitParam(name = "taskNo", value = "任务编号", required = true)
    @GetMapping("/trace/{taskNo}")
    public R<TaskTraceVO> trace(@PathVariable String taskNo) {
        TaskInfo info = taskInfoMapper.selectOne(
                new LambdaQueryWrapper<TaskInfo>().eq(TaskInfo::getTaskNo, taskNo));
        if (info == null) {
            return R.fail("任务不存在");
        }

        TaskTraceVO vo = new TaskTraceVO();
        TaskVO taskVO = BeanUtil.copyProperties(info, TaskVO.class);
        TaskStatus ts = TaskStatus.of(info.getStatus());
        taskVO.setStatusDesc(ts != null ? ts.getDesc() : "");
        vo.setTaskInfo(taskVO);

        try {
            R<List<TaskLog>> logRes = taskLogQueryClient.listByTaskNo(taskNo);
            if (logRes != null && logRes.getCode() == 200 && logRes.getData() != null) {
                vo.setLogs(logRes.getData());
            } else {
                vo.setLogs(Collections.emptyList());
            }
        } catch (Exception e) {
            log.warn("查询任务日志失败 taskNo={}", taskNo, e);
            vo.setLogs(Collections.emptyList());
        }

        try {
            R<List<AlarmRecord>> alarmRes = alarmQueryClient.listByTaskNo(taskNo);
            if (alarmRes != null && alarmRes.getCode() == 200 && alarmRes.getData() != null) {
                vo.setAlarms(alarmRes.getData());
            } else {
                vo.setAlarms(Collections.emptyList());
            }
        } catch (Exception e) {
            log.warn("查询告警记录失败 taskNo={}", taskNo, e);
            vo.setAlarms(Collections.emptyList());
        }

        int stages = 1;
        if (vo.getLogs() != null) {
            stages += vo.getLogs().size();
        }
        if (vo.getAlarms() != null) {
            stages += vo.getAlarms().size();
        }
        vo.setStageCount(stages);
        return R.ok(vo);
    }
}
