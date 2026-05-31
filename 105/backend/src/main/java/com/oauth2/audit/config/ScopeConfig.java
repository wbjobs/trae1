package com.oauth2.audit.config;

import lombok.Getter;
import lombok.RequiredArgsConstructor;
import org.springframework.stereotype.Component;

import java.util.*;

@Component
@RequiredArgsConstructor
@Getter
public class ScopeConfig {

    public enum ScopeGroup {
        BASIC_INFO("基本信息权限", "basic"),
        READ_WRITE("读写权限", "read_write"),
        SENSITIVE_INFO("敏感信息权限", "sensitive"),
        HIGH_RISK("高危权限", "high_risk");

        private final String displayName;
        private final String code;

        ScopeGroup(String displayName, String code) {
            this.displayName = displayName;
            this.code = code;
        }
    }

    private final Map<String, ScopeInfo> scopeInfoMap = new HashMap<>();

    public ScopeConfig() {
        initializeScopes();
    }

    private void initializeScopes() {
        registerScope("profile", "基本信息", ScopeGroup.BASIC_INFO,
            "读取你的昵称和头像", false);
        registerScope("name", "姓名", ScopeGroup.BASIC_INFO,
            "读取你的真实姓名", false);
        registerScope("picture", "头像", ScopeGroup.BASIC_INFO,
            "读取你的头像图片", false);
        registerScope("gender", "性别", ScopeGroup.BASIC_INFO,
            "读取你的性别信息", false);

        registerScope("email", "邮箱", ScopeGroup.SENSITIVE_INFO,
            "读取你的邮箱地址", false);
        registerScope("phone", "手机号", ScopeGroup.SENSITIVE_INFO,
            "读取你的手机号码", false);
        registerScope("address", "地址", ScopeGroup.SENSITIVE_INFO,
            "读取你的收货地址", false);

        registerScope("user:read", "读取用户信息", ScopeGroup.READ_WRITE,
            "读取你的用户资料信息", false);
        registerScope("user:write", "修改用户信息", ScopeGroup.READ_WRITE,
            "修改你的个人资料", false);
        registerScope("post:read", "读取帖子", ScopeGroup.READ_WRITE,
            "读取你发布的帖子内容", false);
        registerScope("post:write", "发帖", ScopeGroup.READ_WRITE,
            "以你的名义发布新帖子", false);
        registerScope("file:read", "读取文件", ScopeGroup.READ_WRITE,
            "访问你的云文件", false);
        registerScope("file:write", "写入文件", ScopeGroup.READ_WRITE,
            "上传和管理你的云文件", false);

        registerScope("admin:all", "管理员权限", ScopeGroup.HIGH_RISK,
            "拥有所有管理员权限，可以执行任何操作", true);
        registerScope("finance:read", "财务读取", ScopeGroup.HIGH_RISK,
            "查看你的账户余额和交易记录", true);
        registerScope("payment:write", "支付权限", ScopeGroup.HIGH_RISK,
            "发起支付交易", true);
        registerScope("data:export", "数据导出", ScopeGroup.HIGH_RISK,
            "导出你的所有个人数据", true);
    }

    private void registerScope(String scope, String displayName, ScopeGroup group,
                               String description, boolean isHighRisk) {
        scopeInfoMap.put(scope, new ScopeInfo(scope, displayName, group, description, isHighRisk));
    }

    public Optional<ScopeInfo> getScopeInfo(String scope) {
        return Optional.ofNullable(scopeInfoMap.get(scope));
    }

    public List<ScopeInfo> getScopesByGroup(String scope) {
        List<ScopeInfo> result = new ArrayList<>();
        for (String s : scope.split(" ")) {
            getScopeInfo(s.trim()).ifPresent(result::add);
        }
        return result;
    }

    public Map<ScopeGroup, List<ScopeInfo>> groupScopes(Collection<String> scopes) {
        Map<ScopeGroup, List<ScopeInfo>> grouped = new LinkedHashMap<>();
        for (ScopeGroup group : ScopeGroup.values()) {
            grouped.put(group, new ArrayList<>());
        }

        for (String scopeStr : scopes) {
            getScopeInfo(scopeStr).ifPresent(info -> {
                grouped.get(info.getGroup()).add(info);
            });
        }

        grouped.values().removeIf(List::isEmpty);
        return grouped;
    }

    public boolean hasHighRiskScope(Collection<String> scopes) {
        return scopes.stream()
            .map(scopeInfoMap::get)
            .filter(Objects::nonNull)
            .anyMatch(ScopeInfo::isHighRisk);
    }

    @Getter
    @RequiredArgsConstructor
    public static class ScopeInfo {
        private final String scope;
        private final String displayName;
        private final ScopeGroup group;
        private final String description;
        private final boolean highRisk;
    }
}
