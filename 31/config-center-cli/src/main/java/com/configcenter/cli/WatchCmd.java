package com.configcenter.cli;

import com.configcenter.client.ConfigCenterClient;
import com.configcenter.proto.WatchConfigResponse;
import picocli.CommandLine;
import picocli.CommandLine.*;

import java.util.concurrent.Callable;
import java.util.concurrent.CountDownLatch;

@Command(name = "watch", description = "Watch config changes via gRPC stream")
public class WatchCmd implements Callable<Integer> {

    @ParentCommand
    private CliMain parent;

    @Option(names = {"-a", "--application"}, required = true)
    private String application;

    @Option(names = {"-e", "--profile"}, required = true)
    private String profile;

    @Option(names = {"-k", "--key"}, defaultValue = "")
    private String key;

    @Option(names = {"--since"}, defaultValue = "0")
    private long sinceVersion;

    @Override
    public Integer call() throws Exception {
        ConfigCenterClient c = parent.client();
        CountDownLatch latch = new CountDownLatch(1);
        Runtime.getRuntime().addShutdownHook(new Thread(latch::countDown));

        c.watch(application, profile, key, sinceVersion, (WatchConfigResponse resp) -> {
            System.out.printf("[%s] event=%s version=%d updatedBy=%s%n%s%n----%n",
                    resp.getEntry().getConfigKey().getKey(),
                    resp.getEventType(),
                    resp.getEntry().getVersion(),
                    resp.getEntry().getUpdatedBy(),
                    resp.getEntry().getContent());
        });

        latch.await();
        c.close();
        return 0;
    }
}
