package com.cdc.core.masking;

import java.util.regex.Matcher;
import java.util.regex.Pattern;

public class MaskingFunctions {

    private static final Pattern PHONE_PATTERN = Pattern.compile("^(\\d{3})\\d{4}(\\d{4})$");
    private static final Pattern ID_CARD_PATTERN = Pattern.compile("^(\\d{6})\\d{8}(\\w{4})$");
    private static final Pattern EMAIL_PATTERN = Pattern.compile("^([\\w.-]+)@([\\w.-]+)$");
    private static final Pattern BANK_CARD_PATTERN = Pattern.compile("^(\\d{4})\\d{8,11}(\\d{4})$");

    public static String maskPhone(String phone) {
        if (phone == null || phone.isEmpty()) {
            return phone;
        }

        String cleaned = phone.replaceAll("[^\\d]", "");
        if (cleaned.length() < 7) {
            return phone;
        }

        Matcher matcher = PHONE_PATTERN.matcher(cleaned);
        if (matcher.find()) {
            return matcher.group(1) + "****" + matcher.group(2);
        }

        return cleaned.substring(0, 3) + "****" + cleaned.substring(cleaned.length() - 4);
    }

    public static String maskIdCard(String idCard) {
        if (idCard == null || idCard.isEmpty()) {
            return idCard;
        }

        String cleaned = idCard.replaceAll("[^\\dXx]", "");
        if (cleaned.length() < 10) {
            return idCard;
        }

        if (cleaned.length() == 18) {
            Matcher matcher = ID_CARD_PATTERN.matcher(cleaned);
            if (matcher.find()) {
                return matcher.group(1) + "********" + matcher.group(2);
            }
        }

        return cleaned.substring(0, 6) + "********" + cleaned.substring(cleaned.length() - 4);
    }

    public static String maskEmail(String email) {
        if (email == null || email.isEmpty()) {
            return email;
        }

        Matcher matcher = EMAIL_PATTERN.matcher(email);
        if (matcher.find()) {
            String username = matcher.group(1);
            String domain = matcher.group(2);

            if (username.length() <= 2) {
                return "*@" + domain;
            }

            String maskedUsername;
            if (username.length() <= 4) {
                maskedUsername = username.charAt(0) + "***";
            } else {
                maskedUsername = username.substring(0, 2) + "***" + username.substring(username.length() - 1);
            }

            return maskedUsername + "@" + domain;
        }

        return email;
    }

    public static String maskEmailDomain(String email, String replacementDomain) {
        if (email == null || email.isEmpty()) {
            return email;
        }

        Matcher matcher = EMAIL_PATTERN.matcher(email);
        if (matcher.find()) {
            return matcher.group(1) + "@" + replacementDomain;
        }

        return email;
    }

    public static String maskName(String name) {
        if (name == null || name.isEmpty()) {
            return name;
        }

        if (name.length() == 1) {
            return "*";
        }

        if (name.length() == 2) {
            return name.charAt(0) + "*";
        }

        StringBuilder sb = new StringBuilder();
        sb.append(name.charAt(0));
        for (int i = 0; i < name.length() - 2; i++) {
            sb.append("*");
        }
        sb.append(name.charAt(name.length() - 1));
        return sb.toString();
    }

    public static String maskBankCard(String bankCard) {
        if (bankCard == null || bankCard.isEmpty()) {
            return bankCard;
        }

        String cleaned = bankCard.replaceAll("[^\\d]", "");
        if (cleaned.length() < 8) {
            return bankCard;
        }

        Matcher matcher = BANK_CARD_PATTERN.matcher(cleaned);
        if (matcher.find()) {
            return matcher.group(1) + "********" + matcher.group(2);
        }

        return cleaned.substring(0, 4) + "********" + cleaned.substring(cleaned.length() - 4);
    }

    public static String maskAddress(String address) {
        if (address == null || address.isEmpty()) {
            return address;
        }

        if (address.length() <= 6) {
            return "***";
        }

        return address.substring(0, 3) + "***" + address.substring(address.length() - 3);
    }

    public static String fullMask(String value) {
        if (value == null || value.isEmpty()) {
            return value;
        }
        return "***";
    }

    public static String maskMiddle(String value, int keepStart, int keepEnd) {
        if (value == null || value.isEmpty()) {
            return value;
        }

        if (value.length() <= keepStart + keepEnd) {
            return fullMask(value);
        }

        StringBuilder sb = new StringBuilder();
        sb.append(value, 0, keepStart);
        for (int i = 0; i < value.length() - keepStart - keepEnd; i++) {
            sb.append("*");
        }
        sb.append(value.substring(value.length() - keepEnd));
        return sb.toString();
    }

    public static String maskByPattern(String value, String pattern, String replacement) {
        if (value == null || value.isEmpty()) {
            return value;
        }
        return value.replaceAll(pattern, replacement);
    }

    public static String maskWithChar(String value, int start, int end, char maskChar) {
        if (value == null || value.isEmpty()) {
            return value;
        }

        if (start < 0) start = 0;
        if (end > value.length()) end = value.length();
        if (start >= end) return value;

        StringBuilder sb = new StringBuilder(value);
        for (int i = start; i < end; i++) {
            sb.setCharAt(i, maskChar);
        }
        return sb.toString();
    }

    public static String hashValue(String value) {
        if (value == null || value.isEmpty()) {
            return value;
        }

        try {
            java.security.MessageDigest digest = java.security.MessageDigest.getInstance("SHA-256");
            byte[] hashBytes = digest.digest(value.getBytes());
            StringBuilder sb = new StringBuilder();
            for (byte b : hashBytes) {
                sb.append(String.format("%02x", b));
            }
            return sb.toString().substring(0, 16);
        } catch (Exception e) {
            return fullMask(value);
        }
    }

    public static String maskPhoneWithFormat(String phone, String format) {
        if (phone == null || phone.isEmpty()) {
            return phone;
        }

        String masked = maskPhone(phone);
        if (format != null && !format.isEmpty()) {
            return masked.replaceFirst("(\\d{3})(\\*{4})(\\d{4})", format);
        }
        return masked;
    }

    public static String maskIdCardWithBirthday(String idCard) {
        if (idCard == null || idCard.length() != 18) {
            return maskIdCard(idCard);
        }

        return idCard.substring(0, 6) + "********" + idCard.substring(14);
    }

    public static String maskCreditCard(String creditCard) {
        if (creditCard == null || creditCard.isEmpty()) {
            return creditCard;
        }

        String cleaned = creditCard.replaceAll("[^\\d]", "");
        if (cleaned.length() < 12) {
            return creditCard;
        }

        return cleaned.substring(0, 4) + "-****-****-" + cleaned.substring(cleaned.length() - 4);
    }
}
