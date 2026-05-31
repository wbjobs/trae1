package com.profile.flink;

import com.profile.config.AppConfig;
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

public class ProfileFlinkJob {

    private static final Logger log = LoggerFactory.getLogger(ProfileFlinkJob.class);

    private final AppConfig config;

    public ProfileFlinkJob(AppConfig config) {
        this.config = config;
    }

    public void execute() throws Exception {
        StreamExecutionEnvironment env = StreamExecutionEnvironment.getExecutionEnvironment();
        env.enableCheckpointing(60000);
        env.getCheckpointConfig().setCheckpointTimeout(300000);
        env.getCheckpointConfig().setMaxConcurrentCheckpoints(1);
        env.getCheckpointConfig().setMinPauseBetweenCheckpoints(30000);

        Properties kafkaProps = new Properties();
        kafkaProps.setProperty(ConsumerConfig.BOOTSTRAP_SERVERS_CONFIG, config.getKafkaBrokers());
        kafkaProps.setProperty(ConsumerConfig.GROUP_ID_CONFIG, config.getKafkaGroupId());
        kafkaProps.setProperty(ConsumerConfig.AUTO_OFFSET_RESET_CONFIG, "latest");

        FlinkKafkaConsumer<UserBehaviorEvent> kafkaSource = new FlinkKafkaConsumer<>(
                config.getKafkaTopic(),
                new KafkaEventSchema(),
                kafkaProps
        );

        DataStream<UserBehaviorEvent> events = env
                .addSource(kafkaSource)
                .name("KafkaSource")
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
                .process(new ProfileAggregateFunction(config))
                .name("ProfileAggregator")
                .addSink(new RedisSinkFunction(config))
                .name("RedisSink");

        env.execute("RealtimeUserProfileJob");
    }

    public static class ProfileAggregateFunction
            extends KeyedProcessFunction<String, UserBehaviorEvent, UserProfileState> {

        private static final long serialVersionUID = 1L;

        private final AppConfig config;

        private transient ValueState<UserProfileState> profileState;

        public ProfileAggregateFunction(AppConfig config) {
            this.config = config;
        }

        @Override
        public void open(Configuration parameters) {
            StateTtlConfig ttlConfig = StateTtlConfig
                    .newBuilder(Time.days(config.getStateTtlDays()))
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
            long now = event.getTimestamp() > 0 ? event.getTimestamp() : System.currentTimeMillis();

            UserProfileState state = profileState.value();
            if (state == null) {
                state = new UserProfileState(userId);
            }

            TagCalculator.applyEvent(state, event);

            profileState.update(state);

            long snapshotInterval = config.getSnapshotIntervalMinutes() * 60 * 1000L;
            if (now - state.getLastUpdateTime() >= snapshotInterval
                    || state.getEventCount() % 1000 == 0) {
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

    public static class RedisSinkFunction extends RichSinkFunction<UserProfileState> {

        private static final long serialVersionUID = 1L;

        private final AppConfig config;
        private transient RedisProfileWriter redisWriter;

        public RedisSinkFunction(AppConfig config) {
            this.config = config;
        }

        @Override
        public void open(Configuration parameters) {
            redisWriter = new RedisProfileWriter(config);
        }

        @Override
        public void invoke(UserProfileState state, Context context) {
            redisWriter.writeProfile(state);
        }

        @Override
        public void close() {
            if (redisWriter != null) {
                redisWriter.close();
            }
        }
    }
}
