package com.distributed.task.scheduler.exception;

import com.distributed.task.common.result.R;
import com.distributed.task.common.result.ResultCode;
import lombok.extern.slf4j.Slf4j;
import org.springframework.dao.DuplicateKeyException;
import org.springframework.http.HttpStatus;
import org.springframework.validation.BindException;
import org.springframework.web.bind.MethodArgumentNotValidException;
import org.springframework.web.bind.annotation.ExceptionHandler;
import org.springframework.web.bind.annotation.ResponseStatus;
import org.springframework.web.bind.annotation.RestControllerAdvice;

@Slf4j
@RestControllerAdvice
public class GlobalExceptionHandler {

    @ExceptionHandler(IllegalStateException.class)
    @ResponseStatus(HttpStatus.OK)
    public R<Void> handleIllegalState(IllegalStateException e) {
        log.warn("业务异常：{}", e.getMessage());
        String msg = e.getMessage();
        if (msg != null && msg.contains("服务不可用")) {
            return R.fail(ResultCode.FAIL.getCode(), msg);
        }
        if (msg != null && msg.contains("任务已存在")) {
            return R.fail(ResultCode.TASK_ALREADY_EXISTS.getCode(), msg);
        }
        return R.fail(ResultCode.IDEMPOTENT_DUPLICATE.getCode(), msg);
    }

    @ExceptionHandler(DuplicateKeyException.class)
    @ResponseStatus(HttpStatus.OK)
    public R<Void> handleDuplicateKey(DuplicateKeyException e) {
        log.warn("DB唯一索引冲突：{}", e.getMessage());
        return R.fail(ResultCode.TASK_ALREADY_EXISTS);
    }

    @ExceptionHandler({MethodArgumentNotValidException.class, BindException.class})
    @ResponseStatus(HttpStatus.OK)
    public R<Void> handleValid(Exception e) {
        return R.fail(ResultCode.PARAM_ERROR.getCode(), "参数校验失败");
    }

    @ExceptionHandler(RuntimeException.class)
    @ResponseStatus(HttpStatus.OK)
    public R<Void> handleRuntime(RuntimeException e) {
        log.error("运行时异常", e);
        return R.fail(ResultCode.FAIL.getCode(), e.getMessage());
    }

    @ExceptionHandler(Exception.class)
    @ResponseStatus(HttpStatus.OK)
    public R<Void> handleException(Exception e) {
        log.error("系统异常", e);
        return R.fail(ResultCode.FAIL.getCode(), "系统异常：" + e.getMessage());
    }
}
