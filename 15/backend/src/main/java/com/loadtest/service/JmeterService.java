package com.loadtest.service;

import com.loadtest.entity.TestConfig;
import com.loadtest.entity.TestResult;
import com.loadtest.entity.TestTask;
import com.loadtest.repository.TestResultRepository;
import com.loadtest.repository.TestTaskRepository;
import com.loadtest.websocket.WebSocketPushService;
import lombok.RequiredArgsConstructor;
import lombok.extern.slf4j.Slf4j;
import org.springframework.beans.factory.annotation.Value;
import org.springframework.stereotype.Service;

import java.io.*;
import java.nio.file.*;
import java.time.LocalDateTime;
import java.util.*;
import java.util.concurrent.*;
import java.util.concurrent.atomic.AtomicLong;

@Slf4j
@Service
@RequiredArgsConstructor
public class JmeterService {

    private final TestTaskRepository testTaskRepository;
    private final TestResultRepository testResultRepository;
    private final WebSocketPushService webSocketPushService;

    @Value("${jmeter.result-dir:./results}")
    private String resultDir;

    @Value("${jmeter.report-dir:./reports}")
    private String reportDir;

    private final Map<Long, Future<?>> runningTasks = new ConcurrentHashMap<>();
    private final Map<Long, Process> runningProcesses = new ConcurrentHashMap<>();
    private final ExecutorService executorService = Executors.newCachedThreadPool();

    private static final long PROCESS_TIMEOUT_HOURS = 24;

    public void executeTask(TestTask task, TestConfig config) {
        Future<?> future = executorService.submit(() -> runLoadTest(task, config));
        runningTasks.put(task.getId(), future);
    }

    private void runLoadTest(TestTask task, TestConfig config) {
        Process process = null;
        try {
            task.setStatus(TestTask.TaskStatus.RUNNING);
            task.setStartedAt(LocalDateTime.now());
            testTaskRepository.save(task);
            webSocketPushService.pushTaskStatus(task.getId(), "RUNNING", Map.of(
                    "startedAt", task.getStartedAt() != null ? task.getStartedAt().toString() : ""
            ));

            Path resultPath = Paths.get(resultDir, "task_" + task.getId() + ".jtl");
            Files.createDirectories(resultPath.getParent());

            process = executeJmeterTest(config, resultPath.toString());
            runningProcesses.put(task.getId(), process);

            boolean completed = waitForProcess(process, task);

            if (!completed) {
                log.warn("JMeter process timed out or was interrupted for task: {}", task.getId());
                task.setStatus(TestTask.TaskStatus.FAILED);
                task.setCompletedAt(LocalDateTime.now());
                task.setResultSummary("Process timed out or was interrupted");
                parsePartialResults(task, resultPath.toString());
                testTaskRepository.save(task);
                webSocketPushService.pushTaskStatus(task.getId(), "FAILED", Map.of(
                        "resultSummary", task.getResultSummary() != null ? task.getResultSummary() : "",
                        "totalRequests", task.getTotalRequests() != null ? task.getTotalRequests() : 0,
                        "successCount", task.getSuccessCount() != null ? task.getSuccessCount() : 0,
                        "failureCount", task.getFailureCount() != null ? task.getFailureCount() : 0
                ));
                return;
            }

            int exitCode = process.exitValue();
            if (exitCode != 0) {
                log.warn("JMeter process exited with code {} for task: {}", exitCode, task.getId());
                task.setStatus(TestTask.TaskStatus.FAILED);
                task.setCompletedAt(LocalDateTime.now());
                task.setResultSummary("JMeter exited with code: " + exitCode);
                parsePartialResults(task, resultPath.toString());
                testTaskRepository.save(task);
                webSocketPushService.pushTaskStatus(task.getId(), "FAILED", Map.of(
                        "resultSummary", task.getResultSummary() != null ? task.getResultSummary() : "",
                        "totalRequests", task.getTotalRequests() != null ? task.getTotalRequests() : 0,
                        "successCount", task.getSuccessCount() != null ? task.getSuccessCount() : 0,
                        "failureCount", task.getFailureCount() != null ? task.getFailureCount() : 0
                ));
                return;
            }

            parseResults(task, resultPath.toString());

            task.setStatus(TestTask.TaskStatus.COMPLETED);
            task.setCompletedAt(LocalDateTime.now());
            testTaskRepository.save(task);
            webSocketPushService.pushTaskStatus(task.getId(), "COMPLETED", Map.of(
                    "totalRequests", task.getTotalRequests() != null ? task.getTotalRequests() : 0,
                    "successCount", task.getSuccessCount() != null ? task.getSuccessCount() : 0,
                    "failureCount", task.getFailureCount() != null ? task.getFailureCount() : 0,
                    "avgResponseTime", task.getAvgResponseTime() != null ? task.getAvgResponseTime() : 0,
                    "throughput", task.getThroughput() != null ? task.getThroughput() : 0,
                    "errorRate", task.getErrorRate() != null ? task.getErrorRate() : 0
            ));

        } catch (Exception e) {
            log.error("Load test execution failed for task: {}", task.getId(), e);
            task.setStatus(TestTask.TaskStatus.FAILED);
            task.setCompletedAt(LocalDateTime.now());
            task.setResultSummary("Execution failed: " + e.getMessage());
            try {
                Path resultPath = Paths.get(resultDir, "task_" + task.getId() + ".jtl");
                parsePartialResults(task, resultPath.toString());
            } catch (Exception ex) {
                log.warn("Failed to parse partial results for task: {}", task.getId(), ex);
            }
            testTaskRepository.save(task);
            webSocketPushService.pushTaskStatus(task.getId(), "FAILED", Map.of(
                    "resultSummary", task.getResultSummary() != null ? task.getResultSummary() : "",
                    "totalRequests", task.getTotalRequests() != null ? task.getTotalRequests() : 0,
                    "successCount", task.getSuccessCount() != null ? task.getSuccessCount() : 0,
                    "failureCount", task.getFailureCount() != null ? task.getFailureCount() : 0
            ));
        } finally {
            cleanupProcess(task.getId(), process);
            runningTasks.remove(task.getId());
            runningProcesses.remove(task.getId());
        }
    }

    private boolean waitForProcess(Process process, TestTask task) throws InterruptedException {
        long timeout = TimeUnit.HOURS.toMillis(PROCESS_TIMEOUT_HOURS);
        long startTime = System.currentTimeMillis();

        ExecutorService readerExecutor = Executors.newSingleThreadExecutor();
        Future<?> readerFuture = readerExecutor.submit(() -> {
            try (BufferedReader reader = new BufferedReader(
                    new InputStreamReader(process.getInputStream()))) {
                String line;
                while ((line = reader.readLine()) != null) {
                    if (line.contains("summary")) {
                        log.info("JMeter progress for task {}: {}", task.getId(), line);
                    } else {
                        log.debug("JMeter: {}", line);
                    }
                }
            } catch (IOException e) {
                log.debug("Error reading JMeter output", e);
            }
        });

        try {
            while (true) {
                if (Thread.currentThread().isInterrupted()) {
                    log.info("Task {} was interrupted", task.getId());
                    return false;
                }

                try {
                    process.exitValue();
                    readerFuture.get(5, TimeUnit.SECONDS);
                    return true;
                } catch (IllegalThreadStateException e) {
                    // Process is still running
                }

                if (System.currentTimeMillis() - startTime > timeout) {
                    log.warn("Task {} timed out after {} hours", task.getId(), PROCESS_TIMEOUT_HOURS);
                    return false;
                }

                Thread.sleep(1000);
            }
        } finally {
            readerFuture.cancel(true);
            readerExecutor.shutdownNow();
        }
    }

    private Process executeJmeterTest(TestConfig config, String resultFilePath) throws Exception {
        List<String> command = buildJmeterCommand(config, resultFilePath);
        log.info("Executing JMeter command for task: {}", String.join(" ", command));

        ProcessBuilder pb = new ProcessBuilder(command);
        pb.redirectErrorStream(true);
        return pb.start();
    }

    private void cleanupProcess(Long taskId, Process process) {
        if (process != null) {
            try {
                if (process.isAlive()) {
                    process.destroyForcibly();
                    process.waitFor(5, TimeUnit.SECONDS);
                    log.info("Destroyed JMeter process for task: {}", taskId);
                }
            } catch (Exception e) {
                log.warn("Error cleaning up process for task: {}", taskId, e);
            }
        }
    }

    private List<String> buildJmeterCommand(TestConfig config, String resultFilePath) {
        List<String> command = new ArrayList<>();

        String jmeterHome = System.getenv().getOrDefault("JMETER_HOME", "");
        String jmeterBin = jmeterHome.isEmpty() ? "jmeter" : jmeterHome + "/bin/jmeter";

        if (System.getProperty("os.name").toLowerCase().contains("win")) {
            jmeterBin += ".bat";
        }

        command.add(jmeterBin);
        command.add("-n");
        command.add("-t");
        command.add(generateJmxFile(config));
        command.add("-l");
        command.add(resultFilePath);
        command.add("-e");
        command.add("-f");

        return command;
    }

    private String generateJmxFile(TestConfig config) {
        try {
            Path jmxPath = Paths.get(resultDir, "config_" + config.getId() + ".jmx");
            Files.createDirectories(jmxPath.getParent());

            String jmxContent = buildJmxContent(config);
            Files.writeString(jmxPath, jmxContent);

            return jmxPath.toString();
        } catch (IOException e) {
            throw new RuntimeException("Failed to generate JMX file", e);
        }
    }

    private String buildJmxContent(TestConfig config) {
        String protocol = config.getProtocol() != null ? config.getProtocol() : "http";
        String domain = config.getDomain() != null && !config.getDomain().isEmpty() 
                ? config.getDomain() : extractDomain(config.getUrl());
        String path = config.getPath() != null && !config.getPath().isEmpty() 
                ? config.getPath() : extractPath(config.getUrl());
        int port = config.getPort() != null ? config.getPort() : (protocol.equals("https") ? 443 : 80);

        StringBuilder jmx = new StringBuilder();
        jmx.append("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");
        jmx.append("<jmeterTestPlan version=\"1.2\" properties=\"5.0\" jmeter=\"5.6\">\n");
        jmx.append("  <hashTree>\n");
        jmx.append("    <TestPlan guiclass=\"TestPlanGui\" testclass=\"TestPlan\" testname=\"Load Test Plan\">\n");
        jmx.append("      <elementProp name=\"TestPlan.user_defined_variables\" elementType=\"Arguments\">\n");
        jmx.append("        <collectionProp name=\"Arguments.arguments\"/>\n");
        jmx.append("      </elementProp>\n");
        jmx.append("      <boolProp name=\"TestPlan.functional_mode\">false</boolProp>\n");
        jmx.append("      <boolProp name=\"TestPlan.serialize_threadgroups\">false</boolProp>\n");
        jmx.append("    </TestPlan>\n");
        jmx.append("    <hashTree>\n");
        jmx.append("      <ThreadGroup guiclass=\"ThreadGroupGui\" testclass=\"ThreadGroup\" testname=\"Thread Group\">\n");
        jmx.append("        <stringProp name=\"ThreadGroup.on_sample_error\">continue</stringProp>\n");

        if (config.getUseLoopCount() != null && config.getUseLoopCount()) {
            jmx.append("        <elementProp name=\"ThreadGroup.main_controller\" elementType=\"LoopController\">\n");
            jmx.append("          <boolProp name=\"LoopController.continue_forever\">false</boolProp>\n");
            jmx.append("          <stringProp name=\"LoopController.loops\">").append(config.getLoopCount()).append("</stringProp>\n");
            jmx.append("        </elementProp>\n");
        } else {
            jmx.append("        <elementProp name=\"ThreadGroup.main_controller\" elementType=\"LoopController\">\n");
            jmx.append("          <boolProp name=\"LoopController.continue_forever\">true</boolProp>\n");
            jmx.append("        </elementProp>\n");
        }

        jmx.append("        <stringProp name=\"ThreadGroup.num_threads\">").append(config.getThreadCount()).append("</stringProp>\n");
        jmx.append("        <stringProp name=\"ThreadGroup.ramp_time\">").append(config.getRampUpTime()).append("</stringProp>\n");

        if (config.getUseLoopCount() == null || !config.getUseLoopCount()) {
            jmx.append("        <stringProp name=\"ThreadGroup.duration\">").append(config.getDuration()).append("</stringProp>\n");
            jmx.append("        <boolProp name=\"ThreadGroup.delayedStart\">true</boolProp>\n");
        }

        jmx.append("        <boolProp name=\"ThreadGroup.scheduler\">false</boolProp>\n");
        jmx.append("      </ThreadGroup>\n");
        jmx.append("      <hashTree>\n");

        if (config.getHeaders() != null && !config.getHeaders().isEmpty()) {
            jmx.append("        <HeaderManager guiclass=\"HeaderPanel\" testclass=\"HeaderManager\" testname=\"HTTP Header Manager\">\n");
            jmx.append("          <collectionProp name=\"HeaderManager.headers\">\n");
            parseHeaders(config.getHeaders()).forEach((key, value) -> {
                jmx.append("            <elementProp name=\"\" elementType=\"Header\">\n");
                jmx.append("              <stringProp name=\"Header.name\">").append(escapeXml(key)).append("</stringProp>\n");
                jmx.append("              <stringProp name=\"Header.value\">").append(escapeXml(value)).append("</stringProp>\n");
                jmx.append("            </elementProp>\n");
            });
            jmx.append("          </collectionProp>\n");
            jmx.append("        </HeaderManager>\n");
        }

        jmx.append("        <HTTPSamplerProxy guiclass=\"HttpTestSampleGui\" testclass=\"HTTPSamplerProxy\" testname=\"HTTP Request\">\n");
        jmx.append("          <boolProp name=\"HTTPSampler.postBodyRaw\">true</boolProp>\n");
        jmx.append("          <elementProp name=\"HTTPsampler.Arguments\" elementType=\"Arguments\">\n");
        jmx.append("            <collectionProp name=\"Arguments.arguments\">\n");

        if (config.getRequestBody() != null && !config.getRequestBody().isEmpty()) {
            jmx.append("              <elementProp name=\"\" elementType=\"HTTPArgument\">\n");
            jmx.append("                <boolProp name=\"HTTPArgument.always_encode\">false</boolProp>\n");
            jmx.append("                <stringProp name=\"Argument.value\">").append(escapeXml(config.getRequestBody())).append("</stringProp>\n");
            jmx.append("                <stringProp name=\"Argument.metadata\">=</stringProp>\n");
            jmx.append("              </elementProp>\n");
        }

        jmx.append("            </collectionProp>\n");
        jmx.append("          </elementProp>\n");
        jmx.append("          <stringProp name=\"HTTPSampler.domain\">").append(escapeXml(domain)).append("</stringProp>\n");
        jmx.append("          <stringProp name=\"HTTPSampler.port\">").append(port).append("</stringProp>\n");
        jmx.append("          <stringProp name=\"HTTPSampler.protocol\">").append(protocol).append("</stringProp>\n");
        jmx.append("          <stringProp name=\"HTTPSampler.contentEncoding\">UTF-8</stringProp>\n");
        jmx.append("          <stringProp name=\"HTTPSampler.path\">").append(escapeXml(path)).append("</stringProp>\n");
        jmx.append("          <stringProp name=\"HTTPSampler.method\">").append(config.getMethod().name()).append("</stringProp>\n");
        jmx.append("          <boolProp name=\"HTTPSampler.follow_redirects\">true</boolProp>\n");
        jmx.append("          <boolProp name=\"HTTPSampler.auto_redirects\">false</boolProp>\n");
        jmx.append("          <boolProp name=\"HTTPSampler.use_keepalive\">true</boolProp>\n");
        jmx.append("          <boolProp name=\"HTTPSampler.DO_MULTIPART_POST\">false</boolProp>\n");
        jmx.append("          <stringProp name=\"HTTPSampler.embedded_url_re\"></stringProp>\n");
        jmx.append("          <stringProp name=\"HTTPSampler.connect_timeout\">")
                .append(config.getConnectionTimeout() != null ? config.getConnectionTimeout() : 10000)
                .append("</stringProp>\n");
        jmx.append("          <stringProp name=\"HTTPSampler.response_timeout\">")
                .append(config.getResponseTimeout() != null ? config.getResponseTimeout() : 30000)
                .append("</stringProp>\n");
        jmx.append("        </HTTPSamplerProxy>\n");

        jmx.append("        <ResponseAssertion guiclass=\"AssertionGui\" testclass=\"ResponseAssertion\" testname=\"Response Assertion\">\n");
        jmx.append("          <collectionProp name=\"Asserion.test_strings\">\n");
        jmx.append("            <stringProp name=\"497725756\">2\\d{2}|3\\d{2}</stringProp>\n");
        jmx.append("          </collectionProp>\n");
        jmx.append("          <stringProp name=\"Assertion.field\">code</stringProp>\n");
        jmx.append("          <stringProp name=\"Assertion.custom_message\"></stringProp>\n");
        jmx.append("          <stringProp name=\"Assertion.assume_success\">false</stringProp>\n");
        jmx.append("          <intProp name=\"Assertion.test_field\">13</intProp>\n");
        jmx.append("          <stringProp name=\"Assertion.assume_success\">false</stringProp>\n");
        jmx.append("          <boolProp name=\"Assertion.equals\">true</boolProp>\n");
        jmx.append("          <boolProp name=\"Assertion.not\">false</boolProp>\n");
        jmx.append("          <boolProp name=\"Assertion.matches\">true</boolProp>\n");
        jmx.append("          <stringProp name=\"Assertion.test_type\">2</stringProp>\n");
        jmx.append("        </ResponseAssertion>\n");

        jmx.append("        <ConstantTimer guiclass=\"ConstantTimerGui\" testclass=\"ConstantTimer\" testname=\"Constant Timer\">\n");
        jmx.append("          <stringProp name=\"ConstantTimer.delay\">100</stringProp>\n");
        jmx.append("        </ConstantTimer>\n");

        if (config.getSimulateDelay() != null && config.getSimulateDelay() && 
            config.getDelayMinMs() != null && config.getDelayMaxMs() != null) {
            jmx.append("        <GaussianRandomTimer guiclass=\"GaussianRandomTimerGui\" testclass=\"GaussianRandomTimer\" testname=\"Delay Simulation\">\n");
            jmx.append("          <stringProp name=\"Delay\">").append(config.getDelayMinMs()).append("</stringProp>\n");
            jmx.append("          <stringProp name=\"Range\">")
                    .append(Math.max(0, config.getDelayMaxMs() - config.getDelayMinMs()))
                    .append("</stringProp>\n");
            jmx.append("        </GaussianRandomTimer>\n");
        }

        if (config.getSimulateError() != null && config.getSimulateError() && 
            config.getErrorProbability() != null && config.getErrorProbability() > 0) {
            jmx.append("        <JSR223Sampler guiclass=\"TestBeanGUI\" testclass=\"JSR223Sampler\" testname=\"Error Simulation\">\n");
            jmx.append("          <stringProp name=\"cacheKey\">ErrorSimulation</stringProp>\n");
            jmx.append("          <stringProp name=\"filename\"></stringProp>\n");
            jmx.append("          <stringProp name=\"parameters\"></stringProp>\n");
            jmx.append("          <stringProp name=\"scriptLanguage\">groovy</stringProp>\n");
            jmx.append("          <stringProp name=\"script\"><![CDATA[");
            jmx.append("if (Math.random() < ").append(config.getErrorProbability()).append(") {");
            jmx.append("  SampleResult.setSuccessful(false);");
            jmx.append("  String[] errorCodes = [");
            if (config.getErrorStatusCodes() != null && !config.getErrorStatusCodes().isEmpty()) {
                String[] codes = config.getErrorStatusCodes().split(",");
                for (int i = 0; i < codes.length; i++) {
                    jmx.append("\"").append(codes[i].trim()).append("\"");
                    if (i < codes.length - 1) jmx.append(",");
                }
            } else {
                jmx.append("\"500\",\"502\",\"503\",\"504\"");
            }
            jmx.append("];");
            jmx.append("  SampleResult.setResponseCode(errorCodes[(int)(Math.random() * errorCodes.length)]);");
            jmx.append("  SampleResult.setResponseMessage('Simulated Error');");
            jmx.append("} else {");
            jmx.append("  SampleResult.setSuccessful(true);");
            jmx.append("  SampleResult.setResponseCode('200');");
            jmx.append("}");
            jmx.append("]]></stringProp>\n");
            jmx.append("        </JSR223Sampler>\n");
        }

        jmx.append("      </hashTree>\n");
        jmx.append("    </hashTree>\n");
        jmx.append("  </hashTree>\n");
        jmx.append("</jmeterTestPlan>");

        return jmx.toString();
    }

    private String escapeXml(String value) {
        if (value == null) return "";
        return value.replace("&", "&amp;")
                .replace("<", "&lt;")
                .replace(">", "&gt;")
                .replace("\"", "&quot;")
                .replace("'", "&apos;");
    }

    private Map<String, String> parseHeaders(String headersJson) {
        Map<String, String> headers = new HashMap<>();
        if (headersJson != null && !headersJson.isEmpty()) {
            try {
                com.alibaba.fastjson2.JSONObject json = com.alibaba.fastjson2.JSON.parseObject(headersJson);
                json.forEach((key, value) -> headers.put(key, String.valueOf(value)));
            } catch (Exception e) {
                log.warn("Failed to parse headers JSON, using as-is: {}", headersJson);
                headers.put("Content-Type", headersJson);
            }
        }
        return headers;
    }

    private String extractDomain(String url) {
        try {
            java.net.URL u = new java.net.URL(url);
            return u.getHost();
        } catch (Exception e) {
            log.warn("Failed to extract domain from URL: {}", url);
            return "localhost";
        }
    }

    private String extractPath(String url) {
        try {
            java.net.URL u = new java.net.URL(url);
            String path = u.getPath();
            String query = u.getQuery();
            return query != null ? path + "?" + query : path;
        } catch (Exception e) {
            log.warn("Failed to extract path from URL: {}", url);
            return "/";
        }
    }

    private void parsePartialResults(TestTask task, String resultFilePath) {
        log.info("Parsing partial results for task: {}", task.getId());
        parseResults(task, resultFilePath);
    }

    private void parseResults(TestTask task, String resultFilePath) {
        List<TestResult> results = new ArrayList<>();
        AtomicLong totalRequests = new AtomicLong(0);
        AtomicLong successCount = new AtomicLong(0);
        AtomicLong failureCount = new AtomicLong(0);
        AtomicLong totalElapsed = new AtomicLong(0);
        AtomicLong minElapsed = new AtomicLong(Long.MAX_VALUE);
        AtomicLong maxElapsed = new AtomicLong(Long.MIN_VALUE);
        List<Long> elapsedList = Collections.synchronizedList(new ArrayList<>());

        File resultFile = new File(resultFilePath);
        if (!resultFile.exists() || resultFile.length() == 0) {
            log.warn("Result file does not exist or is empty: {}", resultFilePath);
            task.setTotalRequests(0L);
            task.setSuccessCount(0L);
            task.setFailureCount(0L);
            return;
        }

        try (BufferedReader reader = new BufferedReader(new FileReader(resultFile))) {
            String headerLine = reader.readLine();
            if (headerLine == null) {
                log.warn("Result file is empty: {}", resultFilePath);
                return;
            }

            String line;
            int batchSize = 0;
            while ((line = reader.readLine()) != null) {
                if (line.trim().isEmpty()) continue;

                String[] columns = line.split(",");
                if (columns.length >= 7) {
                    TestResult result = parseResultLine(task.getId(), columns);
                    if (result != null) {
                        results.add(result);
                        totalRequests.incrementAndGet();
                        if (Boolean.TRUE.equals(result.getSuccess())) {
                            successCount.incrementAndGet();
                        } else {
                            failureCount.incrementAndGet();
                        }
                        if (result.getElapsed() != null) {
                            totalElapsed.addAndGet(result.getElapsed());
                            minElapsed.set(Math.min(minElapsed.get(), result.getElapsed()));
                            maxElapsed.set(Math.max(maxElapsed.get(), result.getElapsed()));
                            elapsedList.add(result.getElapsed());
                        }
                        batchSize++;
                    }
                }
                if (batchSize >= 500) {
                    try {
                        testResultRepository.saveAll(results);
                        results.clear();
                        batchSize = 0;
                    } catch (Exception e) {
                        log.warn("Failed to save batch of results for task: {}", task.getId(), e);
                    }
                }
            }
        } catch (Exception e) {
            log.error("Failed to parse results for task: {}", task.getId(), e);
        }

        if (!results.isEmpty()) {
            try {
                testResultRepository.saveAll(results);
            } catch (Exception e) {
                log.warn("Failed to save remaining results for task: {}", task.getId(), e);
            }
        }

        task.setTotalRequests(totalRequests.get());
        task.setSuccessCount(successCount.get());
        task.setFailureCount(failureCount.get());

        if (totalRequests.get() > 0) {
            task.setAvgResponseTime(totalElapsed.get() / (double) totalRequests.get());
            if (minElapsed.get() != Long.MAX_VALUE) {
                task.setMinResponseTime((double) minElapsed.get());
            }
            if (maxElapsed.get() != Long.MIN_VALUE) {
                task.setMaxResponseTime((double) maxElapsed.get());
            }
            task.setP95ResponseTime(calculatePercentile(elapsedList, 95));
            task.setP99ResponseTime(calculatePercentile(elapsedList, 99));

            long durationSeconds = 1;
            if (task.getStartedAt() != null) {
                LocalDateTime endTime = task.getCompletedAt() != null ? task.getCompletedAt() : LocalDateTime.now();
                durationSeconds = Math.max(1, java.time.Duration.between(task.getStartedAt(), endTime).getSeconds());
            }
            task.setThroughput(totalRequests.get() / (double) durationSeconds);
            task.setErrorRate((failureCount.get() / (double) totalRequests.get()) * 100);
        }

        task.setResultPath(resultFilePath);
    }

    private TestResult parseResultLine(Long taskId, String[] columns) {
        try {
            return TestResult.builder()
                    .taskId(taskId)
                    .timestamp(new java.sql.Timestamp(Long.parseLong(columns[0])).toLocalDateTime())
                    .elapsed(Long.parseLong(columns[1]))
                    .responseCode(columns[3])
                    .success(Boolean.parseBoolean(columns[7]))
                    .bytes(columns.length > 8 ? parseLongSafe(columns[8]) : 0L)
                    .sentBytes(columns.length > 9 ? parseLongSafe(columns[9]) : 0L)
                    .grpThreads(columns.length > 10 ? parseIntSafe(columns[10]) : 0)
                    .allThreads(columns.length > 11 ? parseIntSafe(columns[11]) : 0)
                    .url(columns.length > 12 ? columns[12] : "")
                    .latency(columns.length > 13 ? parseLongSafe(columns[13]) : 0L)
                    .idleTime(columns.length > 14 ? parseLongSafe(columns[14]) : 0L)
                    .connect(columns.length > 15 ? parseLongSafe(columns[15]) : 0L)
                    .build();
        } catch (Exception e) {
            log.debug("Failed to parse result line: {}", String.join(",", columns), e);
            return null;
        }
    }

    private long parseLongSafe(String value) {
        try {
            return Long.parseLong(value);
        } catch (NumberFormatException e) {
            return 0L;
        }
    }

    private int parseIntSafe(String value) {
        try {
            return Integer.parseInt(value);
        } catch (NumberFormatException e) {
            return 0;
        }
    }

    private double calculatePercentile(List<Long> values, int percentile) {
        if (values == null || values.isEmpty()) return 0;
        List<Long> sorted = new ArrayList<>(values);
        Collections.sort(sorted);
        int index = (int) Math.ceil(percentile / 100.0 * sorted.size());
        return sorted.get(Math.min(index, sorted.size() - 1));
    }

    public void stopTask(Long taskId) {
        log.info("Stopping task: {}", taskId);

        Process process = runningProcesses.get(taskId);
        if (process != null && process.isAlive()) {
            try {
                process.destroyForcibly();
                process.waitFor(5, TimeUnit.SECONDS);
                log.info("Destroyed JMeter process for task: {}", taskId);
            } catch (Exception e) {
                log.warn("Error destroying process for task: {}", taskId, e);
            }
            runningProcesses.remove(taskId);
        }

        Future<?> future = runningTasks.get(taskId);
        if (future != null && !future.isDone()) {
            future.cancel(true);
            runningTasks.remove(taskId);
        }

        testTaskRepository.findById(taskId).ifPresent(task -> {
            if (task.getStatus() == TestTask.TaskStatus.RUNNING) {
                task.setStatus(TestTask.TaskStatus.STOPPED);
                task.setCompletedAt(LocalDateTime.now());
                try {
                    Path resultPath = Paths.get(resultDir, "task_" + task.getId() + ".jtl");
                    parsePartialResults(task, resultPath.toString());
                } catch (Exception e) {
                    log.warn("Failed to parse partial results for stopped task: {}", taskId, e);
                }
                testTaskRepository.save(task);
                webSocketPushService.pushTaskStatus(taskId, "STOPPED", Map.of(
                        "totalRequests", task.getTotalRequests() != null ? task.getTotalRequests() : 0,
                        "successCount", task.getSuccessCount() != null ? task.getSuccessCount() : 0,
                        "failureCount", task.getFailureCount() != null ? task.getFailureCount() : 0
                ));
            }
        });
    }

    public boolean isRunning(Long taskId) {
        Future<?> future = runningTasks.get(taskId);
        return future != null && !future.isDone();
    }
}
