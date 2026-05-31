package com.dbagent.interceptor;

import com.dbagent.tracing.TraceContext;
import io.opentelemetry.api.trace.Span;
import io.opentelemetry.context.Context;
import io.opentelemetry.context.Scope;
import net.bytebuddy.implementation.bind.annotation.Origin;
import net.bytebuddy.implementation.bind.annotation.RuntimeType;
import net.bytebuddy.implementation.bind.annotation.SuperCall;

import java.lang.reflect.Method;
import java.util.concurrent.Callable;

public class ConnectionInterceptor {

    @RuntimeType
    public static Object intercept(
            @SuperCall Callable<?> zuper,
            @Origin Method method
    ) throws Exception {

        String methodName = method.getName();

        if (!isTargetMethod(methodName)) {
            return zuper.call();
        }

        Context parentContext = TraceContext.getParentContext();
        if (parentContext != null) {
            Span span = Span.fromContext(parentContext);
            if (span != null) {
                try (Scope scope = span.makeCurrent()) {
                    return zuper.call();
                }
            }
        }

        return zuper.call();
    }

    private static boolean isTargetMethod(String methodName) {
        return methodName.equals("createStatement") ||
                methodName.equals("prepareStatement") ||
                methodName.equals("prepareCall") ||
                methodName.equals("nativeSQL");
    }
}
