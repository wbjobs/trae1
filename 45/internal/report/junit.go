package report

import (
	"encoding/xml"
	"fmt"
	"os"
	"time"
)

type JUnitTestSuites struct {
	XMLName    xml.Name          `xml:"testsuites"`
	TestSuites []JUnitTestSuite  `xml:"testsuite"`
}

type JUnitTestSuite struct {
	Name     string        `xml:"name,attr"`
	Tests    int           `xml:"tests,attr"`
	Failures int           `xml:"failures,attr"`
	Errors   int           `xml:"errors,attr"`
	Skipped  int           `xml:"skipped,attr"`
	Time     float64       `xml:"time,attr"`
	Timestamp string       `xml:"timestamp,attr"`
	Cases    []JUnitTestCase `xml:"testcase"`
}

type JUnitTestCase struct {
	Name      string           `xml:"name,attr"`
	Classname string           `xml:"classname,attr"`
	Time      float64          `xml:"time,attr"`
	SystemOut string           `xml:"system-out,omitempty"`
	Failure   *JUnitFailure    `xml:"failure,omitempty"`
	Error     *JUnitFailure    `xml:"error,omitempty"`
}

type JUnitFailure struct {
	Message string `xml:"message,attr"`
	Type    string `xml:"type,attr"`
	Content string `xml:",cdata"`
}

type SuiteResult struct {
	Name  string
	Cases []CaseResult
}

type CaseResult struct {
	Name       string
	Classname  string
	Duration   time.Duration
	Passed     bool
	Err        error
	Assertions []AssertionResult
	Details    string
}

type AssertionResult struct {
	Passed bool
	Detail string
}

func WriteJUnitXML(path string, suites []SuiteResult) error {
	ts := JUnitTestSuites{}
	for _, s := range suites {
		suite := JUnitTestSuite{
			Name:      s.Name,
			Timestamp: time.Now().UTC().Format("2006-01-02T15:04:05"),
		}
		for _, c := range s.Cases {
			tc := JUnitTestCase{
				Name:      c.Name,
				Classname: c.Classname,
				Time:      c.Duration.Seconds(),
				SystemOut: c.Details,
			}
			if c.Err != nil {
				tc.Error = &JUnitFailure{
					Message: c.Err.Error(),
					Type:    "error",
					Content: c.Err.Error(),
				}
				suite.Errors++
			}
			failed := false
			failMsg := ""
			for _, a := range c.Assertions {
				if !a.Passed {
					failed = true
					failMsg += a.Detail + "\n"
				}
			}
			if failed {
				tc.Failure = &JUnitFailure{
					Message: "assertion failure",
					Type:    "failure",
					Content: failMsg,
				}
				suite.Failures++
			}
			suite.Cases = append(suite.Cases, tc)
		}
		suite.Tests = len(suite.Cases)
		ts.TestSuites = append(ts.TestSuites, suite)
	}
	out, err := xml.MarshalIndent(ts, "", "  ")
	if err != nil {
		return fmt.Errorf("marshal xml: %w", err)
	}
	header := []byte(xml.Header)
	content := append(header, out...)
	if err := os.WriteFile(path, content, 0644); err != nil {
		return fmt.Errorf("write xml: %w", err)
	}
	return nil
}
