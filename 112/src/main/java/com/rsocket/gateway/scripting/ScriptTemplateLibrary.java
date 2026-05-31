package com.rsocket.gateway.scripting;

import com.rsocket.gateway.scripting.model.ScriptDefinition;
import lombok.extern.slf4j.Slf4j;
import org.springframework.stereotype.Component;

import java.util.*;

@Slf4j
@Component
public class ScriptTemplateLibrary {

    private final Map<String, ScriptDefinition> templates = new LinkedHashMap<>();

    public ScriptTemplateLibrary() {
        initTemplates();
    }

    private void initTemplates() {
        templates.put("jwt-authentication", createJwtAuthTemplate());
        templates.put("rate-limiting", createRateLimitingTemplate());
        templates.put("request-logging", createRequestLoggingTemplate());
        templates.put("response-logging", createResponseLoggingTemplate());
        templates.put("error-logging", createErrorLoggingTemplate());
        templates.put("payload-modification", createPayloadModificationTemplate());
        templates.put("metadata-injection", createMetadataInjectionTemplate());
        templates.put("grpc-protocol-converter", createGrpcConverterTemplate());
        templates.put("ip-whitelist", createIpWhitelistTemplate());
        templates.put("parameter-validation", createParameterValidationTemplate());
    }

    private ScriptDefinition createJwtAuthTemplate() {
        String script = """
            import com.auth0.jwt.JWT
            import com.auth0.jwt.algorithms.Algorithm
            import com.auth0.jwt.interfaces.DecodedJWT
            
            def token = metadata?.get("Authorization")?.toString()?.replace("Bearer ", "")
            if (!token) {
                context.reject("Missing authorization token")
                return
            }
            
            try {
                def algorithm = Algorithm.HMAC256("your-secret-key")
                def verifier = JWT.require(algorithm).build()
                DecodedJWT jwt = verifier.verify(token)
                
                context.setAttribute("userId", jwt.getClaim("userId").asString())
                context.setAttribute("roles", jwt.getClaim("roles").asList(String.class))
                context.addMetadata("userId", jwt.getClaim("userId").asString())
                
                log.info("JWT verified for user: {}", jwt.getClaim("userId").asString())
            } catch (Exception e) {
                log.error("JWT verification failed: {}", e.message)
                context.reject("Invalid or expired token")
            }
            """;

        return ScriptDefinition.builder()
                .id("jwt-authentication")
                .name("JWT Authentication")
                .description("Validates JWT tokens from Authorization header")
                .script(script)
                .language("groovy")
                .interceptPoint(ScriptDefinition.InterceptPoint.BEFORE)
                .routes(Collections.emptyList())
                .interactionTypes(Collections.singletonList("*"))
                .priority(100)
                .enabled(true)
                .timeoutMs(50)
                .fallbackStrategy(ScriptDefinition.FallbackStrategy.REJECT)
                .build();
    }

    private ScriptDefinition createRateLimitingTemplate() {
        String script = """
            import java.util.concurrent.ConcurrentHashMap
            import java.util.concurrent.atomic.AtomicInteger
            import java.time.Instant
            
            def rateLimitKey = "rate_limit_" + (context.getAttribute("userId") ?: clientId ?: connectionId)
            def windowSizeSeconds = 60
            def maxRequests = 1000
            
            if (!context.getAttribute("rateLimiter")) {
                context.setAttribute("rateLimiter", new ConcurrentHashMap<String, RateLimitInfo>())
            }
            
            def rateLimiter = context.getAttribute("rateLimiter")
            def now = Instant.now().getEpochSecond()
            def windowStart = now - windowSizeSeconds
            
            def info = rateLimiter.computeIfAbsent(rateLimitKey, k -> new RateLimitInfo(count: 0, windowStart: windowStart))
            
            synchronized(info) {
                if (info.windowStart < windowStart) {
                    info.count = 0
                    info.windowStart = windowStart
                }
                
                info.count++
                
                if (info.count > maxRequests) {
                    context.reject("Rate limit exceeded. Max " + maxRequests + " requests per " + windowSizeSeconds + " seconds")
                    log.warn("Rate limit exceeded for: {}", rateLimitKey)
                }
            }
            
            class RateLimitInfo {
                int count
                long windowStart
            }
            """;

        return ScriptDefinition.builder()
                .id("rate-limiting")
                .name("Request Rate Limiting")
                .description("Limits request rate per client using sliding window")
                .script(script)
                .language("groovy")
                .interceptPoint(ScriptDefinition.InterceptPoint.BEFORE)
                .routes(Collections.emptyList())
                .interactionTypes(Collections.singletonList("*"))
                .priority(90)
                .enabled(true)
                .timeoutMs(50)
                .fallbackStrategy(ScriptDefinition.FallbackStrategy.REJECT)
                .build();
    }

    private ScriptDefinition createRequestLoggingTemplate() {
        String script = """
            log.info("[REQUEST] {} | {} | client={} | conn={} | payload={}",
                    interactionType, route, clientId, connectionId,
                    payload?.toString()?.take(200))
                    
            context.setAttribute("requestStartTime", System.currentTimeMillis())
            """;

        return ScriptDefinition.builder()
                .id("request-logging")
                .name("Request Logging")
                .description("Logs all incoming requests with details")
                .script(script)
                .language("groovy")
                .interceptPoint(ScriptDefinition.InterceptPoint.BEFORE)
                .routes(Collections.emptyList())
                .interactionTypes(Collections.singletonList("*"))
                .priority(1000)
                .enabled(true)
                .timeoutMs(50)
                .fallbackStrategy(ScriptDefinition.FallbackStrategy.LOG_ONLY)
                .build();
    }

    private ScriptDefinition createResponseLoggingTemplate() {
        String script = """
            def startTime = context.getAttribute("requestStartTime")
            def latency = startTime ? (System.currentTimeMillis() - startTime) : -1
            
            log.info("[RESPONSE] {} | {} | client={} | conn={} | latency={}ms | payload={}",
                    interactionType, route, clientId, connectionId, latency,
                    response?.toString()?.take(200))
            """;

        return ScriptDefinition.builder()
                .id("response-logging")
                .name("Response Logging")
                .description("Logs all responses with latency")
                .script(script)
                .language("groovy")
                .interceptPoint(ScriptDefinition.InterceptPoint.AFTER)
                .routes(Collections.emptyList())
                .interactionTypes(Collections.singletonList("*"))
                .priority(1000)
                .enabled(true)
                .timeoutMs(50)
                .fallbackStrategy(ScriptDefinition.FallbackStrategy.LOG_ONLY)
                .build();
    }

    private ScriptDefinition createErrorLoggingTemplate() {
        String script = """
            def startTime = context.getAttribute("requestStartTime")
            def latency = startTime ? (System.currentTimeMillis() - startTime) : -1
            
            log.error("[ERROR] {} | {} | client={} | conn={} | latency={}ms | error={}",
                    interactionType, route, clientId, connectionId, latency,
                    error?.message, error)
            """;

        return ScriptDefinition.builder()
                .id("error-logging")
                .name("Error Logging")
                .description("Logs all errors with details")
                .script(script)
                .language("groovy")
                .interceptPoint(ScriptDefinition.InterceptPoint.ERROR)
                .routes(Collections.emptyList())
                .interactionTypes(Collections.singletonList("*"))
                .priority(1000)
                .enabled(true)
                .timeoutMs(50)
                .fallbackStrategy(ScriptDefinition.FallbackStrategy.LOG_ONLY)
                .build();
    }

    private ScriptDefinition createPayloadModificationTemplate() {
        String script = """
            if (route == "userService.updateUser" && payload instanceof Map) {
                def payloadMap = payload as Map
                
                payloadMap.put("updatedAt", System.currentTimeMillis())
                payloadMap.put("updatedBy", context.getAttribute("userId") ?: "system")
                
                context.modifyPayload(payloadMap)
                log.debug("Modified payload for route: {}", route)
            }
            """;

        return ScriptDefinition.builder()
                .id("payload-modification")
                .name("Payload Modification")
                .description("Demonstrates how to modify request payload")
                .script(script)
                .language("groovy")
                .interceptPoint(ScriptDefinition.InterceptPoint.BEFORE)
                .routes(Collections.emptyList())
                .interactionTypes(Collections.singletonList("*"))
                .priority(500)
                .enabled(false)
                .timeoutMs(50)
                .fallbackStrategy(ScriptDefinition.FallbackStrategy.ALLOW)
                .build();
    }

    private ScriptDefinition createMetadataInjectionTemplate() {
        String script = """
            context.addMetadata("gateway-trace-id", UUID.randomUUID().toString())
            context.addMetadata("gateway-timestamp", System.currentTimeMillis())
            context.addMetadata("gateway-version", "1.0.0")
            
            if (context.getAttribute("userId")) {
                context.addMetadata("user-id", context.getAttribute("userId"))
            }
            """;

        return ScriptDefinition.builder()
                .id("metadata-injection")
                .name("Metadata Injection")
                .description("Injects trace ID and other metadata into requests")
                .script(script)
                .language("groovy")
                .interceptPoint(ScriptDefinition.InterceptPoint.BEFORE)
                .routes(Collections.emptyList())
                .interactionTypes(Collections.singletonList("*"))
                .priority(200)
                .enabled(true)
                .timeoutMs(50)
                .fallbackStrategy(ScriptDefinition.FallbackStrategy.ALLOW)
                .build();
    }

    private ScriptDefinition createGrpcConverterTemplate() {
        String script = """
            if (route?.startsWith("grpc.")) {
                def grpcMethod = route.replace("grpc.", "")
                def grpcService = grpcMethod.substring(0, grpcMethod.lastIndexOf('.'))
                def grpcMethodName = grpcMethod.substring(grpcMethod.lastIndexOf('.') + 1)
                
                def grpcRequest = [
                    service: grpcService,
                    method: grpcMethodName,
                    payload: payload,
                    metadata: metadata
                ]
                
                context.addMetadata("grpc-service", grpcService)
                context.addMetadata("grpc-method", grpcMethodName)
                context.modifyPayload(grpcRequest)
                
                log.debug("Converted RSocket to gRPC: {}.{}", grpcService, grpcMethodName)
            }
            """;

        return ScriptDefinition.builder()
                .id("grpc-protocol-converter")
                .name("RSocket to gRPC Converter")
                .description("Converts RSocket messages to gRPC format for backend calls")
                .script(script)
                .language("groovy")
                .interceptPoint(ScriptDefinition.InterceptPoint.BEFORE)
                .routes(Collections.singletonList("grpc.*"))
                .interactionTypes(Collections.singletonList("*"))
                .priority(300)
                .enabled(false)
                .timeoutMs(50)
                .fallbackStrategy(ScriptDefinition.FallbackStrategy.ALLOW)
                .build();
    }

    private ScriptDefinition createIpWhitelistTemplate() {
        String script = """
            def allowedIps = ["127.0.0.1", "192.168.1.0/24", "10.0.0.0/8"]
            def clientIp = metadata?.get("client-ip")?.toString()
            
            if (clientIp) {
                def allowed = allowedIps.any { ipRange ->
                    if (ipRange.contains("/")) {
                        isIpInRange(clientIp, ipRange)
                    } else {
                        clientIp == ipRange
                    }
                }
                
                if (!allowed) {
                    log.warn("IP not in whitelist: {}", clientIp)
                    context.reject("Access denied: IP not in whitelist")
                }
            }
            
            boolean isIpInRange(String ip, String range) {
                def parts = range.split("/")
                def networkAddress = parts[0]
                def prefix = Integer.parseInt(parts[1])
                
                def ipBytes = InetAddress.getByName(ip).getAddress()
                def networkBytes = InetAddress.getByName(networkAddress).getAddress()
                
                def mask = 0xFFFFFFFF << (32 - prefix)
                def ipInt = bytesToInt(ipBytes) & mask
                def networkInt = bytesToInt(networkBytes) & mask
                
                return ipInt == networkInt
            }
            
            int bytesToInt(byte[] bytes) {
                return ((bytes[0] & 0xFF) << 24) |
                       ((bytes[1] & 0xFF) << 16) |
                       ((bytes[2] & 0xFF) << 8) |
                       (bytes[3] & 0xFF)
            }
            """;

        return ScriptDefinition.builder()
                .id("ip-whitelist")
                .name("IP Whitelist")
                .description("Restricts access based on client IP address whitelist")
                .script(script)
                .language("groovy")
                .interceptPoint(ScriptDefinition.InterceptPoint.BEFORE)
                .routes(Collections.emptyList())
                .interactionTypes(Collections.singletonList("*"))
                .priority(80)
                .enabled(false)
                .timeoutMs(50)
                .fallbackStrategy(ScriptDefinition.FallbackStrategy.REJECT)
                .build();
    }

    private ScriptDefinition createParameterValidationTemplate() {
        String script = """
            if (payload instanceof Map) {
                def payloadMap = payload as Map
                
                def requiredFields = [
                    "userService.createUser": ["email", "password"],
                    "userService.updateUser": ["id"],
                    "orderService.createOrder": ["items", "userId"]
                ]
                
                def fields = requiredFields[route]
                if (fields) {
                    def missing = fields.findAll { !payloadMap.containsKey(it) }
                    if (missing) {
                        context.reject("Missing required fields: " + missing.join(", "))
                        log.warn("Validation failed for {}: missing {}", route, missing)
                    }
                }
                
                def email = payloadMap.get("email")
                if (email && !email.toString().matches("^[A-Za-z0-9+_.-]+@[A-Za-z0-9.-]+$")) {
                    context.reject("Invalid email format")
                }
            }
            """;

        return ScriptDefinition.builder()
                .id("parameter-validation")
                .name("Parameter Validation")
                .description("Validates request parameters before processing")
                .script(script)
                .language("groovy")
                .interceptPoint(ScriptDefinition.InterceptPoint.BEFORE)
                .routes(Collections.emptyList())
                .interactionTypes(Arrays.asList("request-response", "fire-and-forget"))
                .priority(150)
                .enabled(false)
                .timeoutMs(50)
                .fallbackStrategy(ScriptDefinition.FallbackStrategy.REJECT)
                .build();
    }

    public Collection<ScriptDefinition> getTemplates() {
        return templates.values();
    }

    public ScriptDefinition getTemplate(String id) {
        return templates.get(id);
    }

    public Set<String> getTemplateIds() {
        return templates.keySet();
    }
}
