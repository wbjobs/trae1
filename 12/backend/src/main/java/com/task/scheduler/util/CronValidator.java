package com.task.scheduler.util;

import org.quartz.CronExpression;

import java.text.ParseException;
import java.util.regex.Pattern;

public class CronValidator {

    private static final Pattern CRON_PATTERN = Pattern.compile(
            "^([0-9]|,|\\-|\\*|/|\\?|L|W|#)+\\s+([0-9]|,|\\-|\\*|/|\\?|L|W|#)+\\s+([0-9]|,|\\-|\\*|/|\\?|L|W|#)+\\s+([0-9]|,|\\-|\\*|/|\\?|L|W|#)+\\s+([0-9]|,|\\-|\\*|/|\\?|L|W|#|JAN|FEB|MAR|APR|MAY|JUN|JUL|AUG|SEP|OCT|NOV|DEC)+\\s+([0-9]|,|\\-|\\*|/|\\?|L|W|#|MON|TUE|WED|THU|FRI|SAT|SUN)+$",
            Pattern.CASE_INSENSITIVE
    );

    public static boolean isValidCron(String cronExpression) {
        if (cronExpression == null || cronExpression.trim().isEmpty()) {
            return false;
        }

        String trimmedCron = cronExpression.trim();
        String[] parts = trimmedCron.split("\\s+");
        
        if (parts.length != 6 && parts.length != 7) {
            return false;
        }

        if (!CRON_PATTERN.matcher(trimmedCron).matches()) {
            return false;
        }

        try {
            new CronExpression(trimmedCron);
            return true;
        } catch (ParseException e) {
            return false;
        }
    }

    public static String validateCronWithMessage(String cronExpression) {
        if (cronExpression == null || cronExpression.trim().isEmpty()) {
            return "Cron表达式不能为空";
        }

        String trimmedCron = cronExpression.trim();
        String[] parts = trimmedCron.split("\\s+");
        
        if (parts.length != 6 && parts.length != 7) {
            return "Cron表达式必须包含6或7个字段";
        }

        if (!CRON_PATTERN.matcher(trimmedCron).matches()) {
            return "Cron表达式包含非法字符";
        }

        if (!validateSecond(parts[0])) {
            return "秒字段值无效，范围: 0-59";
        }

        if (!validateMinute(parts[1])) {
            return "分钟字段值无效，范围: 0-59";
        }

        if (!validateHour(parts[2])) {
            return "小时字段值无效，范围: 0-23";
        }

        if (!validateDay(parts[3])) {
            return "日字段值无效，范围: 1-31";
        }

        if (!validateMonth(parts[4])) {
            return "月字段值无效，范围: 1-12";
        }

        if (!validateWeek(parts[5])) {
            return "周字段值无效，范围: 1-7 或 SUN-SAT";
        }

        try {
            new CronExpression(trimmedCron);
            return null;
        } catch (ParseException e) {
            return "Cron表达式格式错误: " + e.getMessage();
        }
    }

    private static boolean validateSecond(String field) {
        return validateField(field, 0, 59);
    }

    private static boolean validateMinute(String field) {
        return validateField(field, 0, 59);
    }

    private static boolean validateHour(String field) {
        return validateField(field, 0, 23);
    }

    private static boolean validateDay(String field) {
        if (field.equals("?") || field.equals("L") || field.equals("*")) {
            return true;
        }
        if (field.contains("W") && field.length() <= 3) {
            try {
                int day = Integer.parseInt(field.replace("W", ""));
                return day >= 1 && day <= 31;
            } catch (NumberFormatException e) {
                return false;
            }
        }
        return validateField(field, 1, 31);
    }

    private static boolean validateMonth(String field) {
        String upperField = field.toUpperCase();
        String[] months = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN", "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};
        if (field.equals("*")) return true;
        
        for (String month : months) {
            if (upperField.contains(month)) {
                upperField = upperField.replace(month, "");
            }
        }
        upperField = upperField.replaceAll("[,\\-/*]", "");
        if (upperField.isEmpty()) return true;
        
        try {
            return validateField(field, 1, 12);
        } catch (Exception e) {
            return false;
        }
    }

    private static boolean validateWeek(String field) {
        if (field.equals("?") || field.equals("*")) {
            return true;
        }
        String upperField = field.toUpperCase();
        String[] weeks = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
        for (String week : weeks) {
            if (upperField.contains(week)) {
                upperField = upperField.replace(week, "");
            }
        }
        upperField = upperField.replaceAll("[,\\-/*#L]", "");
        if (upperField.isEmpty()) return true;
        
        if (field.contains("L")) {
            return true;
        }
        if (field.contains("#")) {
            String[] parts = field.split("#");
            if (parts.length == 2) {
                try {
                    int day = Integer.parseInt(parts[0]);
                    int week = Integer.parseInt(parts[1]);
                    return day >= 1 && day <= 7 && week >= 1 && week <= 5;
                } catch (NumberFormatException e) {
                    return false;
                }
            }
        }
        return validateField(field, 1, 7);
    }

    private static boolean validateField(String field, int min, int max) {
        if (field.equals("*") || field.equals("?")) {
            return true;
        }

        String[] values = field.split(",");
        for (String value : values) {
            if (!validateSingleValue(value, min, max)) {
                return false;
            }
        }
        return true;
    }

    private static boolean validateSingleValue(String value, int min, int max) {
        if (value.equals("*") || value.equals("?")) {
            return true;
        }

        if (value.contains("/")) {
            String[] parts = value.split("/");
            if (parts.length == 2) {
                try {
                    int start = parts[0].equals("*") ? min : Integer.parseInt(parts[0]);
                    int interval = Integer.parseInt(parts[1]);
                    return start >= min && start <= max && interval > 0 && interval <= max;
                } catch (NumberFormatException e) {
                    return false;
                }
            }
            return false;
        }

        if (value.contains("-")) {
            String[] parts = value.split("-");
            if (parts.length == 2) {
                try {
                    int start = Integer.parseInt(parts[0]);
                    int end = Integer.parseInt(parts[1]);
                    return start >= min && start <= max && end >= min && end <= max && start <= end;
                } catch (NumberFormatException e) {
                    return false;
                }
            }
            return false;
        }

        try {
            int num = Integer.parseInt(value);
            return num >= min && num <= max;
        } catch (NumberFormatException e) {
            return false;
        }
    }
}
