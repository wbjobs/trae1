package com.oauth2.audit.controller;

import lombok.RequiredArgsConstructor;
import org.springframework.web.bind.annotation.*;

import java.util.HashMap;
import java.util.Map;

@RestController
@RequestMapping("/api/resource")
@RequiredArgsConstructor
@CrossOrigin(origins = "*")
public class ApiController {

    @GetMapping("/profile")
    public Map<String, Object> getProfile() {
        Map<String, Object> profile = new HashMap<>();
        profile.put("message", "Profile data accessed successfully");
        profile.put("scope", "profile");
        return profile;
    }

    @GetMapping("/data")
    public Map<String, Object> getData() {
        Map<String, Object> data = new HashMap<>();
        data.put("message", "Read data accessed successfully");
        data.put("scope", "read");
        return data;
    }

    @PostMapping("/data")
    public Map<String, Object> createData() {
        Map<String, Object> data = new HashMap<>();
        data.put("message", "Data created successfully");
        data.put("scope", "write");
        return data;
    }

    @PutMapping("/data")
    public Map<String, Object> updateData() {
        Map<String, Object> data = new HashMap<>();
        data.put("message", "Data updated successfully");
        data.put("scope", "write");
        return data;
    }

    @DeleteMapping("/data")
    public Map<String, Object> deleteData() {
        Map<String, Object> data = new HashMap<>();
        data.put("message", "Data deleted successfully");
        data.put("scope", "write");
        return data;
    }
}
