package com.distributed.task.alarm.controller;

import com.baomidou.mybatisplus.extension.plugins.pagination.Page;
import com.distributed.task.common.dto.AlarmDTO;
import com.distributed.task.common.entity.AlarmRecord;
import com.distributed.task.common.result.R;
import com.distributed.task.alarm.mapper.AlarmRecordMapper;
import com.distributed.task.alarm.service.AlarmService;
import com.baomidou.mybatisplus.core.conditions.query.LambdaQueryWrapper;
import io.swagger.annotations.Api;
import io.swagger.annotations.ApiImplicitParam;
import io.swagger.annotations.ApiImplicitParams;
import io.swagger.annotations.ApiOperation;
import lombok.RequiredArgsConstructor;
import org.springframework.web.bind.annotation.*;

@Api(tags = "异常告警")
@RestController
@RequestMapping("/alarm")
@RequiredArgsConstructor
public class AlarmController {

    private final AlarmService alarmService;
    private final AlarmRecordMapper alarmRecordMapper;

    @ApiOperation("发送告警（内部）")
    @PostMapping("/send")
    public R<Boolean> send(@RequestBody AlarmDTO dto) {
        return R.ok(alarmService.send(dto));
    }

    @ApiOperation("告警记录分页")
    @ApiImplicitParams({
            @ApiImplicitParam(name = "current", value = "页码", defaultValue = "1"),
            @ApiImplicitParam(name = "size", value = "每页条数", defaultValue = "10"),
            @ApiImplicitParam(name = "alarmType", value = "告警类型"),
            @ApiImplicitParam(name = "sendStatus", value = "发送状态 1成功 0失败")
    })
    @GetMapping("/page")
    public R<Page<AlarmRecord>> page(@RequestParam(defaultValue = "1") int current,
                                 @RequestParam(defaultValue = "10") int size,
                                 @RequestParam(required = false) String alarmType,
                                 @RequestParam(required = false) Integer sendStatus) {
        LambdaQueryWrapper<AlarmRecord> qw = new LambdaQueryWrapper<>();
        if (alarmType != null) {
            qw.eq(AlarmRecord::getAlarmType, alarmType);
        }
        if (sendStatus != null) {
            qw.eq(AlarmRecord::getSendStatus, sendStatus);
        }
        qw.orderByDesc(AlarmRecord::getId);
        return R.ok(alarmRecordMapper.selectPage(new Page<>(current, size), qw));
    }

    @ApiOperation("查询某任务全部告警（内部）")
    @ApiImplicitParam(name = "taskNo", value = "任务编号", required = true)
    @GetMapping("/list-by-task")
    public R<List<AlarmRecord>> listByTaskNo(@RequestParam String taskNo) {
        LambdaQueryWrapper<AlarmRecord> qw = new LambdaQueryWrapper<>();
        qw.eq(AlarmRecord::getTaskNo, taskNo).orderByAsc(AlarmRecord::getId);
        return R.ok(alarmRecordMapper.selectList(qw));
    }
}
