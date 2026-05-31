package com.profile.flink;

import com.profile.config.AppConfig;
import com.profile.config.RecalcConfig;
import com.profile.engine.TagCalculator;
import com.profile.model.UserBehaviorEvent;
import com.profile.model.UserProfileState;
import com.profile.serialize.KafkaEventSchema;
import com.profile.service.RedisProfileWriter;
import org.apache.flink.api.common.eventtime.WatermarkStrategy;
import org.apache.flink.api.common.state.StateTtlConfig;
import org.apache.flink.api.common.state.ValueState;
import org.apache.flink.api.common.state.ValueStateDescriptor;
import org.apache.flink.api.common.time.Time;
import org.apache.flink.configuration.Configuration;
import org.apache.flink.streaming.api.datastream.DataStream;
import org.apache.flink.streaming.api.datastream.KeyedStream;
import org.apache.flink.streaming.api.environment.StreamExecutionEnvironment;
import org.apache.flink.streaming.api.functions.KeyedProcessFunction;
import org.apache.flink.streaming.api.functions.sink.RichSinkFunction;
import org.apache.flink.streaming.connectors.kafka.FlinkKafkaConsumer;
import org.apache.flink.util.Collector;
import org.apache.kafka.clients.consumer.ConsumerConfig;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.util.Properties;

public class ProfileRecalcJob {

    private static final Logger log = LoggerFactory.getLogger(ProfileRecalcJob.class);

    private final AppConfig appConfig;
    private final RecalcConfig recalcConfig;

    public ProfileRecalcJob(AppConfig appConfig, RecalcConfig recalcConfig) {
        this.appConfig = appConfig;
        this.recalcConfig = recalcConfig;
    }

    public void execute() throws Exception {
        StreamExecutionEnvironment env = StreamExecutionEnvironment.getExecutionEnvironment();

        if (recalcConfig.getSavepointPath() != null && !recalcConfig.getSavepointPath().isEmpty()) {
            log.info("Starting recalc job from savepoint: {}", recalcConfig.getSavepointPath());
            env.getCheckpointConfig().setCheckpointStorage(recalcConfig.getSavepointPath());
        }

        env.enableCheckpointing(300000);
        env.getCheckpointConfig().setCheckpointTimeout(600000);
        env.getCheckpointConfig().setMaxConcurrentCheckpoints(1);

        Properties kafkaProps = new Properties();
        kafkaProps.setProperty(ConsumerConfig.BOOTSTRAP_SERVERS_CONFIG, appConfig.getKafkaBrokers());
        kafkaProps.setProperty(ConsumerConfig.GROUP_ID_CONFIG,
                appConfig.getKafkaGroupId() + "-recalc");
        kafkaProps.setProperty(ConsumerConfig.AUTO_OFFSET_RESET_CONFIG, "earliest");

        FlinkKafkaConsumer<UserBehaviorEvent> kafkaSource = new FlinkKafkaConsumer<>(
                appConfig.getKafkaTopic(),
                new KafkaEventSchema(),
                kafkaProps
        );

        if (recalcConfig.getStartTimestamp() > 0) {
            kafkaSource.setStartFromTimestamp(recalcConfig.getStartTimestamp());
        } else {
            kafkaSource.setStartFromEarliest();
        }

        DataStream<UserBehaviorEvent> events = env
                .addSource(kafkaSource)
                .name("Recalc-KafkaSource")
                .filter(event -> {
                    if (event == null) return false;
                    long ts = event.getTimestamp();
                    if (recalcConfig.getEndTimestamp() > 0
                            && ts > recalcConfig.getEndTimestamp()) {
                        return false;
                    }
                    return true;
                })
                .name("TimeRangeFilter")
                .assignTimestampsAndWatermarks(
                        WatermarkStrategy.<UserBehaviorEvent>forMonotonousTimestamps()
                                .withTimestampAssigner((event, ts) ->
                                        event.getTimestamp() > 0
                                                ? event.getTimestamp()
                                                : System.currentTimeMillis())
                );

        KeyedStream<UserBehaviorEvent, String> keyedStream = events
                .keyBy(UserBehaviorEvent::getUserId);

        keyedStream
                .process(new RecalcAggregateFunction(appConfig))
                .name("Recalc-ProfileAggregator")
                .addSink(new RecalcRedisSink(appConfig, recalcConfig))
                .name("Recalc-RedisSink");

        env.execute("RealtimeUserProfile-RecalcJob");
    }

    public static class RecalcAggregateFunction
            extends KeyedProcessFunction<String, UserBehaviorEvent, UserProfileState> {

        private static final long serialVersionUID = 1L;

        private final AppConfig config;
        private transient ValueState<UserProfileState> profileState;

        public RecalcAggregateFunction(AppConfig config) {
            this.config = config;
        }

        @Override
        public void open(Configuration parameters) {
            StateTtlConfig ttlConfig = StateTtlConfig
                    .newBuilder(Time.days(config.getStateTtlDays() * 2))
                    .setUpdateType(StateTtlConfig.UpdateType.OnReadAndWrite)
                    .setStateVisibility(StateTtlConfig.StateVisibility.NeverReturnExpired)
                    .build();

            ValueStateDescriptor<UserProfileState> stateDesc =
                    new ValueStateDescriptor<>("userProfile", UserProfileState.class);
            stateDesc.enableTimeToLive(ttlConfig);
            profileState = getRuntimeContext().getState(stateDesc);
        }

        @Override
        public void processElement(UserBehaviorEvent event, Context ctx,
                                   Collector<UserProfileState> out) throws Exception {

            String userId = ctx.getCurrentKey();

            UserProfileState state = profileState.value();
            if (state == null) {
                state = new UserProfileState(userId);
            }

            TagCalculator.applyEvent(state, event);

            profileState.update(state);

            if (state.getEventCount() % 5000 == 0) {
                out.collect(copyState(state));
            }
        }

        private UserProfileState copyState(UserProfileState s) {
            UserProfileState c = new UserProfileState(s.getUserId());
            c.setTagScores(s.getTagScores());
            c.setLastUpdateTime(s.getLastUpdateTime());
            c.setLastDecayTime(s.getLastDecayTime());
            c.setEventCount(s.getEventCount());
            return c;
        }
    }

    public static class RecalcRedisSink extends RichSinkFunction<UserProfileState> {

        private static final long serialVersionUID = 1L;

        private final AppConfig appConfig;
        private final RecalcConfig recalcConfig;
        private transient RedisProfileWriter redisWriter;

        public RecalcRedisSink(AppConfig appConfig, RecalcConfig recalcConfig) {
            this.appConfig = appConfig;
            this.recalcConfig = recalcConfig;
        }

        @Override
        public void open(Configuration parameters) {
            AppConfig writerConfig = new AppConfig();
            writerConfig.setRedisHost(
                    recalcConfig.getNewRedisHost() != null && !recalcConfig.getNewRedisHost().isEmpty()
                            ? recalcConfig.getNewRedisHost()
                            : appConfig.getRedisHost());
            writerConfig.setRedisPort(recalcConfig.getNewRedisPort() > 0
                    ? recalcConfig.getNewRedisPort()
                    : appConfig.getRedisPort());
            writerConfig.setRedisPassword(
                    recalcConfig.getNewRedisPassword() != null
                            ? recalcConfig.getNewRedisPassword()
                            : appConfig.getRedisPassword());
            writerConfig.setRedisDatabase(recalcConfig.getNewRedisDatabase() > 0
                    ? recalcConfig.getNewRedisDatabase()
                    : appConfig.getRedisDatabase());
            writerConfig.setRedisKeyPrefix(
                    recalcConfig.getNewKeyPrefix() != null && !recalcConfig.getNewKeyPrefix().isEmpty()
                            ? recalcConfig.getNewKeyPrefix()
                            : appConfig.getNewRedisKeyPrefix());
            redisWriter = new RedisProfileWriter(writerConfig);
        }

        @Override
        public void invoke(UserProfileState state, Context context) {
            if (state != null && state.getUserId() != null) {
                redisWriter.writeProfile(state);
            }
        }

        @Override
        public void close() {
            if (redisWriter != null) {
                redisWriter.close();
            }
        }
    }
}
