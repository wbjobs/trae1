package com.dbagent.example;

import com.dbagent.tracing.TraceContext;
import io.opentelemetry.api.trace.Span;
import io.opentelemetry.api.trace.Tracer;
import io.opentelemetry.context.Context;
import io.opentelemetry.context.Scope;

import java.sql.Connection;
import java.sql.DriverManager;
import java.sql.PreparedStatement;
import java.sql.ResultSet;
import java.sql.Statement;

public class TracingExample {

    public static void main(String[] args) {
        try {
            Tracer tracer = io.opentelemetry.api.GlobalOpenTelemetry.getTracer("example");

            Span businessSpan = tracer.spanBuilder("BusinessOperation")
                    .setAttribute("business.type", "order.query")
                    .startSpan();

            try (Scope scope = businessSpan.makeCurrent()) {
                TraceContext.setParentContext(Context.current());

                try {
                    executeDatabaseOperations();
                } finally {
                    TraceContext.clearAll();
                }
            } finally {
                businessSpan.end();
            }

            Thread.sleep(5000);

        } catch (Exception e) {
            e.printStackTrace();
        }
    }

    private static void executeDatabaseOperations() {
        String url = "jdbc:mysql://localhost:3306/test_db";
        String user = "root";
        String password = "password";

        try (Connection conn = DriverManager.getConnection(url, user, password)) {

            try (Statement stmt = conn.createStatement()) {
                ResultSet rs = stmt.executeQuery("SELECT * FROM users WHERE id = 1");
                while (rs.next()) {
                    System.out.println("User: " + rs.getString("name"));
                }
            }

            String sql = "INSERT INTO orders (user_id, amount) VALUES (?, ?)";
            try (PreparedStatement pstmt = conn.prepareStatement(sql)) {
                pstmt.setInt(1, 1);
                pstmt.setDouble(2, 99.99);
                pstmt.executeUpdate();
            }

            try (Statement stmt = conn.createStatement()) {
                int rows = stmt.executeUpdate("UPDATE users SET status = 'active' WHERE id = 1");
                System.out.println("Updated " + rows + " rows");
            }

        } catch (Exception e) {
            e.printStackTrace();
        }
    }
}
