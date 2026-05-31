package com.profile.store.dto;

import jakarta.validation.constraints.Min;
import jakarta.validation.constraints.NotBlank;
import jakarta.validation.constraints.NotNull;

import java.util.List;

public class TagUpdateRequest {

    @NotBlank(message = "tag 不能为空")
    private String tag;

    @NotNull(message = "userIds 不能为空")
    private List<@Min(0) Integer> userIds;

    public String getTag() { return tag; }
    public void setTag(String tag) { this.tag = tag; }

    public List<Integer> getUserIds() { return userIds; }
    public void setUserIds(List<Integer> userIds) { this.userIds = userIds; }
}
