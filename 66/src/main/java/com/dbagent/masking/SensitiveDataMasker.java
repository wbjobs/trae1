package com.dbagent.masking;

import java.util.ArrayList;
import java.util.List;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

public class SensitiveDataMasker {

    private static final String DEFAULT_MASK = "***";

    private final List<MaskingRule> rules;
    private final String mask;

    public SensitiveDataMasker() {
        this(DEFAULT_MASK);
    }

    public SensitiveDataMasker(String mask) {
        this.rules = new ArrayList<>();
        this.mask = mask;
    }

    public void addRule(MaskingRule rule) {
        rules.add(rule);
    }

    public void addRules(List<MaskingRule> ruleList) {
        rules.addAll(ruleList);
    }

    public void clearRules() {
        rules.clear();
    }

    public List<MaskingRule> getRules() {
        return new ArrayList<>(rules);
    }

    public String mask(String sql) {
        if (sql == null || sql.isEmpty()) {
            return sql;
        }

        String result = sql;
        for (MaskingRule rule : rules) {
            result = applyRule(result, rule);
        }
        return result;
    }

    public String maskPreparedStatementParameter(int parameterIndex, String parameterName, Object value) {
        if (value == null) {
            return null;
        }

        String stringValue = String.valueOf(value);

        for (MaskingRule rule : rules) {
            if (rule.getTargetField() != null && rule.getTargetField().length() > 0) {
                Pattern fieldPattern = Pattern.compile(rule.getTargetField(), Pattern.CASE_INSENSITIVE);
                if (fieldPattern.matcher(parameterName).find()) {
                    return mask;
                }
            }

            if (rule.getValuePattern() != null && rule.getValuePattern().length() > 0) {
                Pattern valuePattern = Pattern.compile(rule.getValuePattern());
                if (valuePattern.matcher(stringValue).matches()) {
                    return mask;
                }
            }
        }

        return stringValue;
    }

    private String applyRule(String sql, MaskingRule rule) {
        String result = sql;

        if (rule.getSqlPattern() != null && rule.getSqlPattern().length() > 0) {
            try {
                Pattern pattern = Pattern.compile(rule.getSqlPattern(), Pattern.CASE_INSENSITIVE);
                Matcher matcher = pattern.matcher(result);
                StringBuilder sb = new StringBuilder();
                while (matcher.find()) {
                    String replacement = computeReplacement(matcher, rule);
                    matcher.appendReplacement(sb, Matcher.quoteReplacement(replacement));
                }
                matcher.appendTail(sb);
                result = sb.toString();
            } catch (Exception e) {
                // ignore invalid regex
            }
        }

        if (rule.getFieldPattern() != null && rule.getFieldPattern().length() > 0) {
            try {
                Pattern pattern = Pattern.compile(
                        "(?i)(\\b" + rule.getFieldPattern() + "\\b\\s*=\\s*)(['\"]?)([^'\",\\s)]+)(['\"]?)",
                        Pattern.CASE_INSENSITIVE);
                Matcher matcher = pattern.matcher(result);
                StringBuilder sb = new StringBuilder();
                while (matcher.find()) {
                    String prefix = matcher.group(1);
                    String quote1 = matcher.group(2);
                    String quote2 = matcher.group(4);
                    matcher.appendReplacement(sb, Matcher.quoteReplacement(
                            prefix + quote1 + mask + quote2));
                }
                matcher.appendTail(sb);
                result = sb.toString();
            } catch (Exception e) {
                // ignore invalid regex
            }
        }

        return result;
    }

    private String computeReplacement(Matcher matcher, MaskingRule rule) {
        if (rule.getReplaceGroup() > 0 && rule.getReplaceGroup() <= matcher.groupCount()) {
            StringBuilder sb = new StringBuilder();
            for (int i = 1; i <= matcher.groupCount(); i++) {
                if (i == rule.getReplaceGroup()) {
                    sb.append(mask);
                } else {
                    sb.append(matcher.group(i));
                }
            }
            return sb.toString();
        }
        return matcher.group().replaceFirst(
                Pattern.quote(matcher.group(rule.getReplaceGroup() > 0 ?
                        Math.min(rule.getReplaceGroup(), matcher.groupCount()) : 0)),
                mask);
    }

    public static class MaskingRule {
        private String name;
        private String description;
        private String sqlPattern;
        private String fieldPattern;
        private String valuePattern;
        private String targetField;
        private int replaceGroup;
        private boolean enabled;

        public MaskingRule() {
            this.enabled = true;
            this.replaceGroup = 1;
        }

        public MaskingRule(String name, String sqlPattern) {
            this.name = name;
            this.sqlPattern = sqlPattern;
            this.enabled = true;
            this.replaceGroup = 1;
        }

        public String getName() {
            return name;
        }

        public void setName(String name) {
            this.name = name;
        }

        public String getDescription() {
            return description;
        }

        public void setDescription(String description) {
            this.description = description;
        }

        public String getSqlPattern() {
            return sqlPattern;
        }

        public void setSqlPattern(String sqlPattern) {
            this.sqlPattern = sqlPattern;
        }

        public String getFieldPattern() {
            return fieldPattern;
        }

        public void setFieldPattern(String fieldPattern) {
            this.fieldPattern = fieldPattern;
        }

        public String getValuePattern() {
            return valuePattern;
        }

        public void setValuePattern(String valuePattern) {
            this.valuePattern = valuePattern;
        }

        public String getTargetField() {
            return targetField;
        }

        public void setTargetField(String targetField) {
            this.targetField = targetField;
        }

        public int getReplaceGroup() {
            return replaceGroup;
        }

        public void setReplaceGroup(int replaceGroup) {
            this.replaceGroup = replaceGroup;
        }

        public boolean isEnabled() {
            return enabled;
        }

        public void setEnabled(boolean enabled) {
            this.enabled = enabled;
        }

        @Override
        public String toString() {
            return "MaskingRule{" +
                    "name='" + name + '\'' +
                    ", description='" + description + '\'' +
                    ", sqlPattern='" + sqlPattern + '\'' +
                    ", fieldPattern='" + fieldPattern + '\'' +
                    ", valuePattern='" + valuePattern + '\'' +
                    ", targetField='" + targetField + '\'' +
                    ", replaceGroup=" + replaceGroup +
                    ", enabled=" + enabled +
                    '}';
        }
    }
}
