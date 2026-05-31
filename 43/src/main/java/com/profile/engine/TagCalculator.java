package com.profile.engine;

import com.profile.model.ActionType;
import com.profile.model.Tag;
import com.profile.model.UserBehaviorEvent;
import com.profile.model.UserProfileState;

import java.util.Calendar;
import java.util.TimeZone;

public class TagCalculator {

    private static final long WEEK_MILLIS = 7L * 24 * 60 * 60 * 1000;

    public static void applyEvent(UserProfileState state, UserBehaviorEvent event) {
        ActionType action = ActionType.fromCode(event.getAction());
        if (action == null) return;

        long now = event.getTimestamp() > 0 ? event.getTimestamp() : System.currentTimeMillis();

        applyDecayIfNeeded(state, now);

        for (Tag tag : Tag.values()) {
            if (tag.matchesCategory(event.getCategory())) {
                double score = tag.scoreForAction(action);
                if (score > 0) {
                    score = adjustScoreByContext(tag, event, score);
                    state.addScore(tag.getCode(), score);
                }
            }
        }

        applyTimeBasedTags(state, event, now);

        state.setLastUpdateTime(now);
        state.incrementEventCount();
    }

    private static double adjustScoreByContext(Tag tag, UserBehaviorEvent event, double baseScore) {
        ActionType action = ActionType.fromCode(event.getAction());
        if (action == null) return baseScore;

        switch (tag) {
            case HIGH_SPENDER:
                if (action == ActionType.PURCHASE && event.getPrice() > 500) {
                    return baseScore * 2.0;
                }
                if (action == ActionType.PURCHASE && event.getPrice() > 1000) {
                    return baseScore * 3.0;
                }
                break;
            case BARGAIN_HUNTER:
                if (action == ActionType.VIEW && event.getPrice() > 0) {
                    return baseScore;
                }
                break;
            case IMPULSIVE_BUYER:
                if (action == ActionType.PURCHASE) {
                    long sessionGap = System.currentTimeMillis() - event.getTimestamp();
                    if (sessionGap < 60000) {
                        return baseScore * 1.5;
                    }
                }
                break;
            default:
                break;
        }
        return baseScore;
    }

    private static void applyTimeBasedTags(UserProfileState state, UserBehaviorEvent event, long now) {
        ActionType action = ActionType.fromCode(event.getAction());
        if (action == null) return;

        Calendar cal = Calendar.getInstance(TimeZone.getDefault());
        cal.setTimeInMillis(now);
        int hour = cal.get(Calendar.HOUR_OF_DAY);
        int dayOfWeek = cal.get(Calendar.DAY_OF_WEEK);

        if (hour >= 0 && hour < 6) {
            state.addScore(Tag.NIGHT_ACTIVE.getCode(), Tag.NIGHT_ACTIVE.scoreForAction(action));
        }

        if (hour >= 5 && hour < 9) {
            state.addScore(Tag.EARLY_BIRD.getCode(), Tag.EARLY_BIRD.scoreForAction(action));
        }

        if (dayOfWeek == Calendar.SATURDAY || dayOfWeek == Calendar.SUNDAY) {
            state.addScore(Tag.WEEKEND_SHOPPER.getCode(), Tag.WEEKEND_SHOPPER.scoreForAction(action));
        }
    }

    public static void applyDecayIfNeeded(UserProfileState state, long now) {
        long lastDecay = state.getLastDecayTime();
        if (lastDecay <= 0) {
            state.setLastDecayTime(now);
            return;
        }

        long elapsed = now - lastDecay;
        if (elapsed < WEEK_MILLIS) {
            return;
        }

        double weeks = (double) elapsed / WEEK_MILLIS;
        for (Tag tag : Tag.values()) {
            double factor = Math.pow(tag.getWeeklyDecayFactor(), weeks);
            state.applyDecay(tag.getCode(), factor);
        }
        state.setLastDecayTime(now);
    }
}
