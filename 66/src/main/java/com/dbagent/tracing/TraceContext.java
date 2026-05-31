package com.dbagent.tracing;

import io.opentelemetry.api.trace.Span;
import io.opentelemetry.api.trace.SpanContext;
import io.opentelemetry.context.Context;
import io.opentelemetry.context.Scope;

public class TraceContext {

    private static final ThreadLocal<Context> PARENT_CONTEXT_HOLDER = new ThreadLocal<>();
    private static final ThreadLocal<Scope> CURRENT_SCOPE_HOLDER = new ThreadLocal<>();

    private TraceContext() {
    }

    public static void setParentContext(Context context) {
        if (context != null) {
            PARENT_CONTEXT_HOLDER.set(context);
        }
    }

    public static Context getParentContext() {
        Context context = PARENT_CONTEXT_HOLDER.get();
        if (context == null) {
            context = Context.current();
        }
        return context;
    }

    public static void clearParentContext() {
        PARENT_CONTEXT_HOLDER.remove();
    }

    public static void setCurrentScope(Scope scope) {
        CURRENT_SCOPE_HOLDER.set(scope);
    }

    public static Scope getCurrentScope() {
        return CURRENT_SCOPE_HOLDER.get();
    }

    public static void clearCurrentScope() {
        CURRENT_SCOPE_HOLDER.remove();
    }

    public static void clearAll() {
        PARENT_CONTEXT_HOLDER.remove();
        CURRENT_SCOPE_HOLDER.remove();
    }

    public static SpanContext getCurrentSpanContext() {
        return Span.current().getSpanContext();
    }

    public static boolean hasParentContext() {
        return PARENT_CONTEXT_HOLDER.get() != null;
    }

    public static Context extractParentContext() {
        Context parentContext = getParentContext();
        clearParentContext();
        return parentContext;
    }
}
