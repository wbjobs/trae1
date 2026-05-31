package cmd

import (
	"fmt"
	"os"

	"github.com/spf13/cobra"
	"github.com/spf13/viper"
)

var (
	cfgFile      string
	cloud        string
	outputDir    string
	format       bool
	driftDetect  bool
	importMode   bool
	interactive  bool
)

func NewRootCommand() *cobra.Command {
	rootCmd := &cobra.Command{
		Use:   "tfgen",
		Short: "Terraform配置生成工具",
		Long:  `通过分析云资源使用情况，自动生成Terraform HCL配置代码`,
	}

	rootCmd.PersistentFlags().StringVar(&cfgFile, "config", "", "配置文件路径")
	rootCmd.PersistentFlags().StringVar(&cloud, "cloud", "all", "云服务商: aws, aliyun, tencent, all")
	rootCmd.PersistentFlags().StringVar(&outputDir, "output", "./terraform", "输出目录")
	rootCmd.PersistentFlags().BoolVar(&format, "format", false, "格式化HCL输出")
	rootCmd.PersistentFlags().BoolVar(&driftDetect, "drift-detect", false, "检测资源偏差")
	rootCmd.PersistentFlags().BoolVar(&importMode, "import", false, "生成Import配置")
	rootCmd.PersistentFlags().BoolVar(&interactive, "interactive", false, "交互式模式，手动调整依赖关系")

	rootCmd.AddCommand(NewGenerateCommand())
	rootCmd.AddCommand(NewDriftCommand())
	rootCmd.AddCommand(NewImportCommand())
	rootCmd.AddCommand(NewGraphCommand())
	rootCmd.AddCommand(NewCycleCommand())
	rootCmd.AddCommand(NewCostCommand())

	cobra.OnInitialize(initConfig)

	return rootCmd
}

func initConfig() {
	if cfgFile != "" {
		viper.SetConfigFile(cfgFile)
	} else {
		home, err := os.UserHomeDir()
		cobra.CheckErr(err)
		viper.AddConfigPath(home)
		viper.SetConfigType("yaml")
		viper.SetConfigName(".tfgen")
	}

	viper.AutomaticEnv()

	if err := viper.ReadInConfig(); err == nil {
		fmt.Fprintln(os.Stderr, "使用配置文件:", viper.ConfigFileUsed())
	}
}