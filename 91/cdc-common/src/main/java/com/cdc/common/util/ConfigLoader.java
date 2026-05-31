package com.cdc.common.util;

import com.cdc.common.config.CdcConfig;
import com.fasterxml.jackson.databind.DeserializationFeature;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.dataformat.yaml.YAMLFactory;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.io.File;
import java.io.IOException;
import java.io.InputStream;

public class ConfigLoader {

    private static final Logger logger = LoggerFactory.getLogger(ConfigLoader.class);
    private static final ObjectMapper yamlMapper = new ObjectMapper(new YAMLFactory())
            .configure(DeserializationFeature.FAIL_ON_UNKNOWN_PROPERTIES, false);
    private static final ObjectMapper jsonMapper = new ObjectMapper()
            .configure(DeserializationFeature.FAIL_ON_UNKNOWN_PROPERTIES, false);

    public static CdcConfig loadConfig(String path) throws IOException {
        File file = new File(path);
        if (!file.exists()) {
            logger.warn("Config file not found at {}, trying classpath", path);
            return loadFromClasspath(path);
        }
        return loadFromFile(file);
    }

    public static CdcConfig loadFromFile(File file) throws IOException {
        logger.info("Loading config from file: {}", file.getAbsolutePath());
        String fileName = file.getName().toLowerCase();
        ObjectMapper mapper = fileName.endsWith(".yaml") || fileName.endsWith(".yml") ? yamlMapper : jsonMapper;
        return mapper.readValue(file, CdcConfig.class);
    }

    public static CdcConfig loadFromClasspath(String resource) throws IOException {
        logger.info("Loading config from classpath: {}", resource);
        try (InputStream is = ConfigLoader.class.getClassLoader().getResourceAsStream(resource)) {
            if (is == null) {
                throw new IOException("Resource not found in classpath: " + resource);
            }
            String resourceLower = resource.toLowerCase();
            ObjectMapper mapper = resourceLower.endsWith(".yaml") || resourceLower.endsWith(".yml") ? yamlMapper : jsonMapper;
            return mapper.readValue(is, CdcConfig.class);
        }
    }
}
