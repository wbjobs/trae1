package main

import (
	"encoding/json"
	"fmt"
	"io"
	"os"
	"text/tabwriter"
	"time"
)

type ReportFormat string

const (
	FormatTable ReportFormat = "table"
	FormatJSON  ReportFormat = "json"
)

func WriteReport(report AuditReport, format ReportFormat, out io.Writer) error {
	if report.GeneratedAt == "" {
		report.GeneratedAt = time.Now().UTC().Format(time.RFC3339)
	}
	switch format {
	case FormatJSON:
		enc := json.NewEncoder(out)
		enc.SetIndent("", "  ")
		return enc.Encode(report)
	case FormatTable:
		return writeTableReport(report, out)
	default:
		return fmt.Errorf("unsupported report format: %s", format)
	}
}

func writeTableReport(r AuditReport, out io.Writer) error {
	tw := tabwriter.NewWriter(out, 0, 4, 2, ' ', 0)
	fmt.Fprintf(tw, "=== K8s NetworkPolicy Audit Report ===\n")
	fmt.Fprintf(tw, "Generated:\t%s\n", r.GeneratedAt)
	fmt.Fprintf(tw, "Total observed flows:\t%d\n", r.TotalFlows)
	fmt.Fprintf(tw, "Violations:\t%d\n", len(r.Violations))
	fmt.Fprintf(tw, "Impacted pods:\t%d\n\n", len(r.ImpactedPods))

	if len(r.ImpactedPods) > 0 {
		fmt.Fprintf(tw, "Impacted pods:\n")
		for _, p := range r.ImpactedPods {
			fmt.Fprintf(tw, "  - %s\n", p)
		}
		fmt.Fprintln(tw)
	}

	if len(r.Violations) == 0 {
		fmt.Fprintln(tw, "No unauthorized connections detected.")
		return tw.Flush()
	}

	fmt.Fprintln(tw, "Violations:")
	fmt.Fprintf(tw, "SrcPod\tDstPod\tProto\tDstPort\tCount\tBytes\tSrcIPState\tDstIPState\tReason\n")
	fmt.Fprintf(tw, "------\t------\t-----\t-------\t-----\t-----\t----------\t----------\t------\n")
	for _, v := range r.Violations {
		fmt.Fprintf(tw, "%s\t%s\t%s\t%d\t%d\t%d\t%s\t%s\t%s\n",
			nilSafe(v.SrcPod),
			nilSafe(v.DstPod),
			v.Proto,
			v.DstPort,
			v.Count,
			v.Bytes,
			nilSafe(v.SrcIPState),
			nilSafe(v.DstIPState),
			v.Reason)
	}
	return tw.Flush()
}

func nilSafe(s string) string {
	if s == "" {
		return "<unknown>"
	}
	return s
}

func SaveReport(report AuditReport, format ReportFormat, path string) error {
	f, err := os.Create(path)
	if err != nil {
		return err
	}
	defer f.Close()
	return WriteReport(report, format, f)
}
