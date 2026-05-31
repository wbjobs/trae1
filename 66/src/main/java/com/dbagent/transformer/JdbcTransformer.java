package com.dbagent.transformer;

import com.dbagent.interceptor.ConnectionInterceptor;
import com.dbagent.interceptor.PreparedStatementInterceptor;
import com.dbagent.interceptor.StatementInterceptor;
import net.bytebuddy.agent.builder.AgentBuilder;
import net.bytebuddy.description.type.TypeDescription;
import net.bytebuddy.dynamic.DynamicType;
import net.bytebuddy.implementation.MethodDelegation;
import net.bytebuddy.matcher.ElementMatcher;
import net.bytebuddy.matcher.ElementMatchers;
import net.bytebuddy.utility.JavaModule;

import java.sql.Connection;
import java.sql.PreparedStatement;
import java.sql.Statement;

public class JdbcTransformer {

    public static AgentBuilder.Transformer createStatementTransformer() {
        return new AgentBuilder.Transformer() {
            @Override
            public DynamicType.Builder<?> transform(
                    DynamicType.Builder<?> builder,
                    TypeDescription typeDescription,
                    ClassLoader classLoader,
                    JavaModule module) {
                return builder.method(ElementMatchers.named("execute")
                                .or(ElementMatchers.named("executeQuery"))
                                .or(ElementMatchers.named("executeUpdate"))
                                .or(ElementMatchers.named("executeBatch"))
                                .or(ElementMatchers.named("executeLargeUpdate")))
                        .intercept(MethodDelegation.to(StatementInterceptor.class));
            }
        };
    }

    public static AgentBuilder.Transformer createPreparedStatementTransformer() {
        return new AgentBuilder.Transformer() {
            @Override
            public DynamicType.Builder<?> transform(
                    DynamicType.Builder<?> builder,
                    TypeDescription typeDescription,
                    ClassLoader classLoader,
                    JavaModule module) {
                return builder.method(ElementMatchers.named("execute")
                                .or(ElementMatchers.named("executeQuery"))
                                .or(ElementMatchers.named("executeUpdate"))
                                .or(ElementMatchers.named("executeBatch"))
                                .or(ElementMatchers.named("executeLargeUpdate")))
                        .intercept(MethodDelegation.to(PreparedStatementInterceptor.class));
            }
        };
    }

    public static AgentBuilder.Transformer createConnectionTransformer() {
        return new AgentBuilder.Transformer() {
            @Override
            public DynamicType.Builder<?> transform(
                    DynamicType.Builder<?> builder,
                    TypeDescription typeDescription,
                    ClassLoader classLoader,
                    JavaModule module) {
                return builder.method(ElementMatchers.named("createStatement")
                                .or(ElementMatchers.named("prepareStatement"))
                                .or(ElementMatchers.named("prepareCall"))
                                .or(ElementMatchers.named("nativeSQL")))
                        .intercept(MethodDelegation.to(ConnectionInterceptor.class));
            }
        };
    }

    public static ElementMatcher<? super TypeDescription> statementMatcher() {
        return new ElementMatcher<TypeDescription>() {
            @Override
            public boolean matches(TypeDescription target) {
                return !target.getName().equals("java.sql.Statement") &&
                        target.isAssignableTo(Statement.class) &&
                        !target.isAssignableTo(PreparedStatement.class) &&
                        !target.getName().contains("Proxy") &&
                        !target.getName().contains("Wrapper");
            }
        };
    }

    public static ElementMatcher<? super TypeDescription> preparedStatementMatcher() {
        return new ElementMatcher<TypeDescription>() {
            @Override
            public boolean matches(TypeDescription target) {
                return !target.getName().equals("java.sql.PreparedStatement") &&
                        target.isAssignableTo(PreparedStatement.class) &&
                        !target.getName().contains("Proxy") &&
                        !target.getName().contains("Wrapper");
            }
        };
    }

    public static ElementMatcher<? super TypeDescription> connectionMatcher() {
        return new ElementMatcher<TypeDescription>() {
            @Override
            public boolean matches(TypeDescription target) {
                return !target.getName().equals("java.sql.Connection") &&
                        target.isAssignableTo(Connection.class) &&
                        !target.getName().contains("Proxy") &&
                        !target.getName().contains("Wrapper");
            }
        };
    }
}
