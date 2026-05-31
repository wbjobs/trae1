package com.orchestrator.cli;

import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.SerializationFeature;
import com.orchestrator.model.DAGDefinition;
import com.orchestrator.model.TaskRuntime;
import com.orchestrator.model.TaskStatus;
import com.orchestrator.result.ResultStore;
import com.orchestrator.zookeeper.ZkPaths;
import com.orchestrator.zookeeper.ZkService;
import org.apache.commons.cli.*;

import java.nio.file.Files;
import java.nio.file.Paths;
import java.util.*;

public class CliMain {

    private static final String DEFAULT_ZK = "127.0.0.1:2181";

    public static void main(String[] args) {
        Options options = buildOptions();
        CommandLineParser parser = new DefaultParser();

        try {
            CommandLine cmd = parser.parse(options, args);

            if (cmd.hasOption("help") || cmd.getArgs().length == 0) {
                printHelp(options);
                return;
            }

            String zkConnect = cmd.getOptionValue("zk", DEFAULT_ZK);
            String action = cmd.getArgs()[0];

            ZkService zkService = new ZkService(zkConnect);
            zkService.connect();

            try {
                switch (action) {
                    case "submit":
                        handleSubmit(cmd, zkService);
                        break;
                    case "status":
                        handleStatus(cmd, zkService);
                        break;
                    case "result":
                        handleResult(cmd, zkService);
                        break;
                    case "list":
                        handleList(zkService);
                        break;
                    case "dag":
                        handleDag(cmd, zkService);
                        break;
                    default:
                        System.out.println("Unknown action: " + action);
                        System.out.println("Available actions: submit, status, result, list, dag");
                }
            } finally {
                zkService.close();
            }

        } catch (ParseException e) {
            System.out.println("Error parsing command: " + e.getMessage());
            printHelp(options);
        } catch (Exception e) {
            System.out.println("Error: " + e.getMessage());
            e.printStackTrace();
        }
    }

    private static Options buildOptions() {
        Options options = new Options();
        options.addOption("h", "help", false, "Print help message");
        options.addOption("z", "zk", true, "ZooKeeper connect string (default: 127.0.0.1:2181)");
        options.addOption("f", "file", true, "DAG definition JSON file path");
        options.addOption("d", "dag", true, "DAG ID to query status");
        options.addOption("t", "task", true, "Task ID to query status");
        return options;
    }

    private static void printHelp(Options options) {
        HelpFormatter formatter = new HelpFormatter();
        System.out.println("Distributed Task Orchestrator CLI");
        System.out.println();
        System.out.println("Usage:");
        System.out.println("  java -jar task-orchestrator.jar [options] <action> [args]");
        System.out.println();
        System.out.println("Actions:");
        System.out.println("  submit  -f <dag-file.json>          Submit a DAG definition");
        System.out.println("  status  -d <dagId> [-t <taskId>]    Query DAG/task status");
        System.out.println("  result  -d <dagId> -t <taskId>      Query task output result");
        System.out.println("  list                                  List all DAGs");
        System.out.println("  dag     -d <dagId>                   Show DAG definition");
        System.out.println();
        formatter.printHelp(" ", options);
    }

    private static void handleSubmit(CommandLine cmd, ZkService zkService) throws Exception {
        String filePath = cmd.getOptionValue("file");
        if (filePath == null) {
            System.out.println("Error: --file option is required for submit");
            return;
        }

        String json = new String(Files.readAllBytes(Paths.get(filePath)));
        ObjectMapper mapper = zkService.getObjectMapper();
        DAGDefinition dag = mapper.readValue(json, DAGDefinition.class);

        if (dag.getId() == null || dag.getId().isEmpty()) {
            dag.setId("dag_" + System.currentTimeMillis() + "_" + UUID.randomUUID().toString().substring(0, 4));
        }

        String dagPath = ZkPaths.dagPath(dag.getId());
        zkService.ensurePath(ZkPaths.DAGS);
        zkService.createPersistent(dagPath, dag);

        String tasksPath = ZkPaths.dagTasksPath(dag.getId());
        zkService.ensurePath(tasksPath);

        System.out.println("DAG submitted successfully!");
        System.out.println("  DAG ID: " + dag.getId());
        System.out.println("  Name: " + dag.getName());
        System.out.println("  Tasks: " + dag.getTasks().size());
        for (Map.Entry<String, com.orchestrator.model.TaskNode> entry : dag.getTasks().entrySet()) {
            System.out.println("    - " + entry.getKey() + " [" + entry.getValue().getType() + "] " +
                    "deps=" + dag.getDependencies(entry.getKey()));
        }
    }

    private static void handleStatus(CommandLine cmd, ZkService zkService) throws Exception {
        String dagId = cmd.getOptionValue("dag");
        if (dagId == null) {
            System.out.println("Error: --dag option is required for status");
            return;
        }

        String taskId = cmd.getOptionValue("task");
        String tasksPath = ZkPaths.dagTasksPath(dagId);

        if (!zkService.exists(tasksPath)) {
            System.out.println("DAG not found: " + dagId);
            return;
        }

        List<String> taskIds = zkService.getChildren(tasksPath);
        if (taskIds.isEmpty()) {
            System.out.println("No tasks found for DAG: " + dagId);
            return;
        }

        ObjectMapper mapper = zkService.getObjectMapper();
        mapper.enable(SerializationFeature.INDENT_OUTPUT);

        ResultStore resultStore = new ResultStore(zkService);

        System.out.println("DAG Status: " + dagId);
        System.out.println(String.join("", Collections.nCopies(80, "-")));

        int successCount = 0, failedCount = 0, runningCount = 0, pendingCount = 0, retryingCount = 0;

        for (String tid : taskIds) {
            if (taskId != null && !tid.equals(taskId)) continue;

            TaskRuntime runtime = zkService.getData(tasksPath + "/" + tid, TaskRuntime.class);
            if (runtime == null) continue;

            String statusBar = "  ";
            switch (runtime.getStatus()) {
                case SUCCESS:   statusBar = "OK"; successCount++; break;
                case FAILED:    statusBar = "FAIL"; failedCount++; break;
                case RUNNING:   statusBar = "RUN"; runningCount++; break;
                case PENDING:   statusBar = "PEND"; pendingCount++; break;
                case RETRYING:  statusBar = "RETRY"; retryingCount++; break;
                case TIMEOUT:   statusBar = "TMOUT"; failedCount++; break;
            }

            System.out.printf("[%5s] %s (retry=%d, worker=%s)%n",
                    statusBar, tid, runtime.getRetryCount(),
                    runtime.getWorkerId() != null ? runtime.getWorkerId() : "-");

            if (runtime.getErrorMessage() != null && !runtime.getErrorMessage().isEmpty()) {
                System.out.println("       Error: " + runtime.getErrorMessage());
            }
            if (runtime.getResult() != null && !runtime.getResult().isEmpty()) {
                String resultPreview = runtime.getResult().length() > 200
                        ? runtime.getResult().substring(0, 200) + "..."
                        : runtime.getResult();
                System.out.println("       Output: " + resultPreview.replace("\n", " "));
            }

            if (resultStore.hasResult(dagId, tid)) {
                System.out.println("       Result stored at: /orchestrator/results/" + dagId + "/" + tid);
            }

            if (runtime.getStartTime() > 0) {
                long duration = runtime.getEndTime() > 0 ? runtime.getEndTime() - runtime.getStartTime() : -1;
                System.out.println("       Start: " + new Date(runtime.getStartTime()) +
                        (duration > 0 ? ", Duration: " + duration + "ms" : ""));
            }
        }

        System.out.println(String.join("", Collections.nCopies(80, "-")));
        System.out.printf("Summary: %d total, %d success, %d failed, %d running, %d pending, %d retrying%n",
                taskIds.size(), successCount, failedCount, runningCount, pendingCount, retryingCount);
    }

    private static void handleResult(CommandLine cmd, ZkService zkService) {
        String dagId = cmd.getOptionValue("dag");
        String taskId = cmd.getOptionValue("task");

        if (dagId == null || taskId == null) {
            System.out.println("Error: --dag and --task options are required for result");
            return;
        }

        ResultStore resultStore = new ResultStore(zkService);
        String rawResult = resultStore.getResultRaw(dagId, taskId);

        if (rawResult == null) {
            System.out.println("No result found for task " + dagId + "/" + taskId);
            System.out.println("(Task may not have completed yet or produced no output)");
            return;
        }

        System.out.println("Result for " + dagId + "/" + taskId + ":");
        System.out.println(String.join("", Collections.nCopies(80, "-")));
        try {
            ObjectMapper mapper = zkService.getObjectMapper();
            Object parsed = mapper.readValue(rawResult, Object.class);
            mapper.enable(SerializationFeature.INDENT_OUTPUT);
            System.out.println(mapper.writeValueAsString(parsed));
        } catch (Exception e) {
            System.out.println(rawResult);
        }
        System.out.println(String.join("", Collections.nCopies(80, "-")));
        System.out.println("Size: " + rawResult.getBytes().length + " bytes");
    }

    private static void handleList(ZkService zkService) {
        List<String> dagIds = zkService.getChildren(ZkPaths.DAGS);
        if (dagIds.isEmpty()) {
            System.out.println("No DAGs found.");
            return;
        }
        System.out.println("DAGs:");
        for (String dagId : dagIds) {
            DAGDefinition dag = zkService.getData(ZkPaths.dagPath(dagId), DAGDefinition.class);
            if (dag != null) {
                System.out.println("  " + dagId + " - " + dag.getName() + " (" + dag.getTasks().size() + " tasks)");
            } else {
                System.out.println("  " + dagId + " (data unavailable)");
            }
        }
    }

    private static void handleDag(CommandLine cmd, ZkService zkService) {
        String dagId = cmd.getOptionValue("dag");
        if (dagId == null) {
            System.out.println("Error: --dag option is required for dag");
            return;
        }

        DAGDefinition dag = zkService.getData(ZkPaths.dagPath(dagId), DAGDefinition.class);
        if (dag == null) {
            System.out.println("DAG not found: " + dagId);
            return;
        }

        System.out.println("DAG Definition: " + dagId);
        System.out.println("  Name: " + dag.getName());
        System.out.println("  Tasks:");
        for (Map.Entry<String, com.orchestrator.model.TaskNode> entry : dag.getTasks().entrySet()) {
            com.orchestrator.model.TaskNode task = entry.getValue();
            System.out.println("    " + entry.getKey() + ":");
            System.out.println("      Type: " + task.getType());
            System.out.println("      Timeout: " + (task.getTimeoutMs() / 1000) + "s");
            System.out.println("      MaxRetries: " + task.getMaxRetries());
            System.out.println("      Dependencies: " + dag.getDependencies(entry.getKey()));
        }
    }
}
