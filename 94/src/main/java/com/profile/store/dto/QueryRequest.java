package com.profile.store.dto;

import jakarta.validation.constraints.NotBlank;
import jakarta.validation.constraints.NotNull;

public class QueryRequest {

    @NotBlank(message = "dsl 不能为空")
    private String dsl;

    private int offset = 0;
    private int limit = 100;

    public String getDsl() { return dsl; }
    public void setDsl(String dsl) { this.dsl = dsl; }

    public int getOffset() { return offset; }
    public void setOffset(int offset) { this.offset = offset; }

    public int getLimit() { return limit; }
    public void setLimit(int limit) { this.limit = limit; }
}
