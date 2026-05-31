package com.dbagent;

import com.dbagent.config.AgentConfig;
import com.dbagent.indexadvisor.SlowSqlDetector;
import com.dbagent.indexadvisor.StatisticsCollector;
import com.dbagent.indexadvisor.WebhookNotifier;
import com.dbagent.interceptor.PreparedStatementInterceptor;
import com.dbagent.interceptor.StatementInterceptor;
import com.dbagent.masking.MaskingConfig;
import com.dbagent.tracing.TracingInitializer;
import com.dbagent.transformer.JdbcTransformer;
import net.bytebuddy.agent.builder.AgentBuilder;
import net.bytebuddy.description.type.TypeDescription;
import net.bytebuddy.dynamic.DynamicType;
import net.bytebuddy.utility.JavaModule;

import java.lang.instrument.Instrumentation;

public class AgentPremain {

    private static volatile AgentConfig agentConfig;

    public static void premain(String agentArgs, Instrumentation inst) {
        initialize(agentArgs, inst, false);
    }

    public static void agentmain(String agentArgs, Instrumentation inst) {
        initialize(agentArgs, inst, true);
    }

    private static synchronized void initialize(String agentArgs, Instrumentation inst, boolean isAgentMain) {
        System.out.println("[DB-Tracing-Agent] Starting initialization..."
                + (isAgentMain ? " (dynamic attach mode)" : ""));

        try {
            agentConfig = AgentConfig.fromArgs(agentArgs);
            System.out.println("[DB-Tracing-Agent] Configuration: " + agentConfig);

            StatementInterceptor.setConfig(agentConfig);
            PreparedStatementInterceptor.setConfig(agentConfig);

            if (agentConfig.isMaskingEnabled()) {
                MaskingConfig maskingConfig = MaskingConfig.getInstance(
                        agentConfig.getMaskingConfigPath(),
                        agentConfig.getMaskingReloadIntervalMs());
                maskingConfig.getMasker();
                if (agentConfig.isMaskingHotReload()) {
                    maskingConfig.enableHotReload();
                }
                Runtime.getRuntime().addShutdownHook(new Thread(maskingConfig::shutdown));
            }

            if (agentConfig.isIndexAdvisorEnabled()) {
                initializeIndexAdvisor(agentConfig);
            }

            TracingInitializer.initialize(agentConfig);

            installByteBuddyAgent(inst);

            System.out.println("[DB-Tracing-Agent] Initialization completed successfully!");
        } catch (Exception e) {
            System.err.println("[DB-Tracing-Agent] Initialization failed: " + e.getMessage());
            e.printStackTrace();
        }
    }

    private static void initializeIndexAdvisor(AgentConfig config) {
        try {
            StatisticsCollector statsCollector = StatisticsCollector.getInstance();
            statsCollector.initialize(300000, 3600000);

            WebhookNotifier webhookNotifier = null;
            if (config.getWebhookUrl() != null && !config.getWebhookUrl().isEmpty()) {
                webhookNotifier = new WebhookNotifier(
                        config.getWebhookUrl(),
                        config.getWebhookType(),
                        config.getWebhookSecret());
            }

            SlowSqlDetector.getInstance().initialize(
                    config.getSlowSqlThresholdMs(),
                    config.getIndexSuggestionCooldownMs(),
                    webhookNotifier,
                    config.isIndexAutoExecute());

            Runtime.getRuntime().addShutdownHook(new Thread(() -> {
                SlowSqlDetector.getInstance().shutdown();
                statsCollector.shutdown();
            }));

            System.out.println("[DB-Tracing-Agent] Index advisor initialized, threshold: "
                    + config.getSlowSqlThresholdMs() + "ms");
        } catch (Exception e) {
            System.err.println("[DB-Tracing-Agent] Failed to initialize index advisor: "
                    + e.getMessage());
        }
    }

    private static void installByteBuddyAgent(Instrumentation inst) {
        try {
            AgentBuilder agentBuilder = new AgentBuilder.Default()
                    .with(AgentBuilder.RedefinitionStrategy.RETRANSFORMATION)
                    .with(AgentBuilder.TypeStrategy.Default.REDEFINE)
                    .with(AgentBuilder.Listener.StreamWriting.toSystemError().withErrorsOnly())
                    .with(AgentBuilder.DescriptionStrategy.Default.POOL_ONLY)
                    .disableClassFormatChanges()
                    .with(AgentBuilder.InstallationListener.StreamWriting.toSystemOut());

            agentBuilder = agentBuilder
                    .type(JdbcTransformer.connectionMatcher())
                    .transform(JdbcTransformer.createConnectionTransformer())
                    .asDecorator();

            agentBuilder = agentBuilder
                    .type(JdbcTransformer.statementMatcher())
                    .transform(JdbcTransformer.createStatementTransformer())
                    .asDecorator();

            agentBuilder = agentBuilder
                    .type(JdbcTransformer.preparedStatementMatcher())
                    .transform(JdbcTransformer.createPreparedStatementTransformer())
                    .asDecorator();

            agentBuilder.installOn(inst);

            System.out.println("[DB-Tracing-Agent] ByteBuddy agent installed successfully");
        } catch (Exception e) {
            System.err.println("[DB-Tracing-Agent] Failed to install ByteBuddy agent: "
                    + e.getMessage());
            e.printStackTrace();
        }
    }

    public static AgentConfig getAgentConfig() {
        return agentConfig;
    }
}
