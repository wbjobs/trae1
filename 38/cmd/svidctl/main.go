package main

import (
	"context"
	"encoding/json"
	"fmt"
	"os"
	"text/tabwriter"
	"time"

	"github.com/spf13/cobra"
	clientv3 "go.etcd.io/etcd/client/v3"

	"github.com/spiffe-gateway/svid-gateway/internal/policy"
	"github.com/spiffe-gateway/svid-gateway/internal/registry"
)
var etcdEndpoints []string

func main() {
	root := &cobra.Command{Use: "svidctl", Short: "SVID Gateway CLI"}
	root.PersistentFlags().StringSliceVar(&etcdEndpoints, "etcd", []string{"localhost:2379"}, "etcd endpoints")

	root.AddCommand(newSVIDCmd(), newPolicyCmd(), newIdentCmd())
	if err := root.Execute(); err != nil {
		fmt.Fprintln(os.Stderr, err)
		os.Exit(1)
	}
}

func newSVIDCmd() *cobra.Command {
	c := &cobra.Command{Use: "svid", Short: "Manage SVIDs"}
	c.AddCommand(&cobra.Command{
		Use:   "list",
		Short: "List current issued SVIDs (via SPIRE Workload API)",
		RunE: func(cmd *cobra.Command, args []string) error {
			w := tabwriter.NewWriter(os.Stdout, 0, 0, 2, ' ', 0)
			fmt.Fprintln(w, "TYPE\tSPIFFE ID\tSERIAL\tEXPIRES")
			return w.Flush()
		},
	})
	return c
}

func newPolicyCmd() *cobra.Command {
	c := &cobra.Command{Use: "policy", Short: "Manage authorization policies"}
	c.AddCommand(&cobra.Command{
		Use:   "list",
		Short: "List all policies from etcd",
		RunE: func(cmd *cobra.Command, args []string) error {
			ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
			defer cancel()
			cli, err := clientv3.New(clientv3.Config{Endpoints: etcdEndpoints, DialTimeout: 3 * time.Second})
			if err != nil {
				return err
			}
			defer cli.Close()
			resp, err := cli.Get(ctx, "/svid-gateway/policies/", clientv3.WithPrefix())
			if err != nil {
				return err
			}
			w := tabwriter.NewWriter(os.Stdout, 0, 0, 2, ' ', 0)
			fmt.Fprintln(w, "ID\tNAME\tSOURCE\tDEST\tMETHOD\tPATH\tEFFECT\tENABLED")
			for _, kv := range resp.Kvs {
				var p policy.Policy
				if err := json.Unmarshal(kv.Value, &p); err != nil {
					continue
				}
				fmt.Fprintf(w, "%s\t%s\t%s\t%s\t%v\t%s\t%s\t%v\n",
					p.ID, p.Name, p.Source, p.Destination, p.Methods, p.Path, p.Effect, p.Enabled)
			}
			return w.Flush()
		},
	})
	return c
}

func newIdentCmd() *cobra.Command {
	c := &cobra.Command{Use: "identity", Short: "Manage service identities"}
	c.AddCommand(&cobra.Command{
		Use:   "list",
		Short: "List all registered service identities and certificate expiry",
		RunE: func(cmd *cobra.Command, args []string) error {
			ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
			defer cancel()
			reg, err := registry.New(etcdEndpoints, "/svid-gateway/identities/")
			if err != nil {
				return err
			}
			defer reg.Close()
			list, err := reg.List(ctx)
			if err != nil {
				return err
			}
			w := tabwriter.NewWriter(os.Stdout, 0, 0, 2, ' ', 0)
			fmt.Fprintln(w, "SPIFFE ID\tNAME\tSELECTOR\tREGISTERED\tEXPIRES")
			for _, s := range list {
				fmt.Fprintf(w, "%s\t%s\t%s\t%s\t%s\n",
					s.SPIFFEID, s.Name, s.Selector,
					s.RegisteredAt.Format(time.RFC3339),
					s.ExpiresAt.Format(time.RFC3339))
			}
			return w.Flush()
		},
	})
	return c
}
