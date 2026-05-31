package k8s

import (
	"bytes"
	"context"
	"fmt"
	"io"
	"time"

	appsv1 "k8s.io/api/apps/v1"
	corev1 "k8s.io/api/core/v1"
	storagev1 "k8s.io/api/storage/v1"
	"k8s.io/apimachinery/pkg/api/resource"
	metav1 "k8s.io/apimachinery/pkg/apis/meta/v1"
	"k8s.io/client-go/kubernetes"
	"k8s.io/client-go/kubernetes/scheme"
	"k8s.io/client-go/rest"
	"k8s.io/client-go/tools/clientcmd"
	"k8s.io/client-go/tools/remotecommand"
	"go.uber.org/zap"
)

type K8sClient struct {
	clientset *kubernetes.Clientset
	config    *rest.Config
	logger    *zap.Logger
	namespace string
}

type K8sConfig struct {
	Kubeconfig string
	Namespace   string
	InCluster  bool
}

func NewK8sClient(cfg *K8sConfig, logger *zap.Logger) (*K8sClient, error) {
	var config *rest.Config
	var err error

	if cfg.InCluster {
		config, err = rest.InClusterConfig()
	} else {
		config, err = clientcmd.BuildConfigFromFlags("", cfg.Kubeconfig)
	}
	if err != nil {
		return nil, fmt.Errorf("failed to create K8s config: %w", err)
	}

	clientset, err := kubernetes.NewForConfig(config)
	if err != nil {
		return nil, fmt.Errorf("failed to create K8s client: %w", err)
	}

	return &K8sClient{
		clientset: clientset,
		config:    config,
		logger:   logger,
		namespace: cfg.Namespace,
	}, nil
}

func (c *K8sClient) GetPVC(ctx context.Context, namespace, name string) (*corev1.PersistentVolumeClaim, error) {
	pvc, err := c.clientset.CoreV1().PersistentVolumeClaims(namespace).Get(ctx, name, metav1.GetOptions{})
	if err != nil {
		return nil, fmt.Errorf("failed to get PVC %s/%s: %w", namespace, name, err)
	}
	return pvc, nil
}

func (c *K8sClient) GetPV(ctx context.Context, name string) (*corev1.PersistentVolume, error) {
	pv, err := c.clientset.CoreV1().PersistentVolumes().Get(ctx, name, metav1.GetOptions{})
	if err != nil {
		return nil, fmt.Errorf("failed to get PV %s: %w", name, err)
	}
	return pv, nil
}

type RBDVolumeInfo struct {
	Pool      string
	Image     string
	Monitor   string
	User      string
	Keyring   string
}

func (c *K8sClient) ExtractRBDInfoFromPV(pv *corev1.PersistentVolume) (*RBDVolumeInfo, error) {
	if pv.Spec.CSI != nil && pv.Spec.CSI.Driver == "rbd.csi.ceph.com" {
		volCtx := pv.Spec.CSI.VolumeAttributes
		if volCtx == nil {
			return nil, fmt.Errorf("CSI volume attributes not found")
		}

		return &RBDVolumeInfo{
			Pool:    volCtx["pool"],
			Image:   volCtx["image"],
			Monitor: volCtx["monitors"],
			User:    volCtx["userID"],
		}, nil
	}

	if pv.Spec.RBD != nil {
		return &RBDVolumeInfo{
			Pool:    pv.Spec.RBD.RBDPool,
			Image:   pv.Spec.RBD.RBDImage,
			Monitor: pv.Spec.RBD.Monitors[0],
			User:    pv.Spec.RBD.RadosUser,
		}, nil
	}

	return nil, fmt.Errorf("PV %s is not an RBD volume", pv.Name)
}

func (c *K8sClient) CreatePVCFromRBD(ctx context.Context, namespace, pvcName, rbdImage, pool, storageClass string, size resource.Quantity) (*corev1.PersistentVolumeClaim, error) {
	c.logger.Info("Creating PVC from RBD image",
		zap.String("namespace", namespace),
		zap.String("pvc", pvcName),
		zap.String("rbd_image", rbdImage),
	)

	pvc := &corev1.PersistentVolumeClaim{
		ObjectMeta: metav1.ObjectMeta{
			Name:      pvcName,
			Namespace: namespace,
		},
		Spec: corev1.PersistentVolumeClaimSpec{
			AccessModes: []corev1.PersistentVolumeAccessMode{
				corev1.ReadWriteOnce,
			},
			Resources: corev1.VolumeResourceRequirements{
				Requests: corev1.ResourceList{
					corev1.ResourceStorage: size,
				},
			},
		},
	}

	if storageClass != "" {
		pvc.Spec.StorageClassName = &storageClass
	}

	pvc.Spec.DataSource = &corev1.TypedLocalObjectReference{
		Name: rbdImage,
		Kind: "PersistentVolumeClaim",
	}

	createdPVC, err := c.clientset.CoreV1().PersistentVolumeClaims(namespace).Create(ctx, pvc, metav1.CreateOptions{})
	if err != nil {
		return nil, fmt.Errorf("failed to create PVC: %w", err)
	}

	c.logger.Info("PVC created successfully",
		zap.String("pvc", pvcName),
	)
	return createdPVC, nil
}

func (c *K8sClient) CreatePVForRBD(ctx context.Context, pvName, rbdImage, pool, monitors, user, keyring, fsType string, size resource.Quantity) (*corev1.PersistentVolume, error) {
	c.logger.Info("Creating PV for RBD image",
		zap.String("pv", pvName),
		zap.String("rbd_image", rbdImage),
	)

	pv := &corev1.PersistentVolume{
		ObjectMeta: metav1.ObjectMeta{
			Name: pvName,
		},
		Spec: corev1.PersistentVolumeSpec{
			Capacity: corev1.ResourceList{
				corev1.ResourceStorage: size,
			},
			AccessModes: []corev1.PersistentVolumeAccessMode{
				corev1.ReadWriteOnce,
			},
			PersistentVolumeSource: corev1.PersistentVolumeSource{
				CSI: &corev1.CSIPersistentVolumeSource{
					Driver:       "rbd.csi.ceph.com",
					VolumeHandle: fmt.Sprintf("%s-%s", pool, rbdImage),
					VolumeAttributes: map[string]string{
						"clusterID": pool,
						"pool":       pool,
						"image":       rbdImage,
						"imageFeatures": "layering",
						"imageFormat": "2",
					},
					NodeStageSecretRef: &corev1.SecretReference{
						Name:      "ceph-csi-secret",
						Namespace: "ceph-csi",
					},
					ControllerPublishSecretRef: &corev1.SecretReference{
						Name:      "ceph-csi-secret",
						Namespace: "ceph-csi",
					},
					ControllerExpandSecretRef: &corev1.SecretReference{
						Name:      "ceph-csi-secret",
						Namespace: "ceph-csi",
					},
				},
			},
		},
	}

	createdPV, err := c.clientset.CoreV1().PersistentVolumes().Create(ctx, pv, metav1.CreateOptions{})
	if err != nil {
		return nil, fmt.Errorf("failed to create PV: %w", err)
	}

	c.logger.Info("PV created successfully",
		zap.String("pv", pvName),
	)
	return createdPV, nil
}

func (c *K8sClient) DeletePVC(ctx context.Context, namespace, name string) error {
	c.logger.Info("Deleting PVC",
		zap.String("namespace", namespace),
		zap.String("pvc", name),
	)

	err := c.clientset.CoreV1().PersistentVolumeClaims(namespace).Delete(ctx, name, metav1.DeleteOptions{})
	if err != nil {
		return fmt.Errorf("failed to delete PVC: %w", err)
	}

	c.logger.Info("PVC deleted successfully")
	return nil
}

func (c *K8sClient) DeletePV(ctx context.Context, name string) error {
	c.logger.Info("Deleting PV",
		zap.String("pv", name),
	)

	err := c.clientset.CoreV1().PersistentVolumes().Delete(ctx, name, metav1.DeleteOptions{})
	if err != nil {
		return fmt.Errorf("failed to delete PV: %w", err)
	}

	c.logger.Info("PV deleted successfully")
	return nil
}

func (c *K8sClient) GetPod(ctx context.Context, namespace, name string) (*corev1.Pod, error) {
	return c.clientset.CoreV1().Pods(namespace).Get(ctx, name, metav1.GetOptions{})
}

func (c *K8sClient) ListPodsByLabel(ctx context.Context, namespace string, labelSelector string) ([]corev1.Pod, error) {
	pods, err := c.clientset.CoreV1().Pods(namespace).List(ctx, metav1.ListOptions{
		LabelSelector: labelSelector,
	})
	if err != nil {
		return nil, fmt.Errorf("failed to list pods: %w", err)
	}
	return pods.Items, nil
}

func (c *K8sClient) ExecInPod(ctx context.Context, namespace, podName, containerName string, command []string) (string, error) {
	c.logger.Info("Executing command in pod",
		zap.String("pod", podName),
		zap.String("container", containerName),
		zap.Strings("command", command),
	)

	req := c.clientset.CoreV1().RESTClient().Post().
		Resource("pods").
		Name(podName).
		Namespace(namespace).
		SubResource("exec")

	execOpts := &corev1.PodExecOptions{
		Container: containerName,
		Command:   command,
		Stdin:     false,
		Stdout:    true,
		Stderr:    true,
		TTY:       false,
	}

	req.VersionedParams(execOpts, scheme.ParameterCodec)

	var stdout, stderr bytes.Buffer
	exec, err := remotecommand.NewSPDYExecutor(c.config, "POST", req.URL())
	if err != nil {
		return "", fmt.Errorf("failed to create executor: %w", err)
	}

	err = exec.StreamWithContext(ctx, remotecommand.StreamOptions{
		Stdout: &stdout,
		Stderr: &stderr,
	})
	if err != nil {
		return stdout.String(), fmt.Errorf("command execution failed: %w, stderr: %s", err, stderr.String())
	}

	return stdout.String(), nil
}

func (c *K8sClient) GetDeployment(ctx context.Context, namespace, name string) (*appsv1.Deployment, error) {
	return c.clientset.AppsV1().Deployments(namespace).Get(ctx, name, metav1.GetOptions{})
}

func (c *K8sClient) ScaleDeployment(ctx context.Context, namespace, name string, replicas int32) error {
	deployment, err := c.GetDeployment(ctx, namespace, name)
	if err != nil {
		return err
	}

	deployment.Spec.Replicas = &replicas

	_, err = c.clientset.AppsV1().Deployments(namespace).Update(ctx, deployment, metav1.UpdateOptions{})
	if err != nil {
		return fmt.Errorf("failed to scale deployment: %w", err)
	}

	c.logger.Info("Deployment scaled",
		zap.String("deployment", name),
		zap.Int32("replicas", replicas),
	)
	return nil
}

func (c *K8sClient) GetStatefulSet(ctx context.Context, namespace, name string) (*appsv1.StatefulSet, error) {
	return c.clientset.AppsV1().StatefulSets(namespace).Get(ctx, name, metav1.GetOptions{})
}

func (c *K8sClient) ScaleStatefulSet(ctx context.Context, namespace, name string, replicas int32) error {
	statefulSet, err := c.GetStatefulSet(ctx, namespace, name)
	if err != nil {
		return err
	}

	statefulSet.Spec.Replicas = &replicas

	_, err = c.clientset.AppsV1().StatefulSets(namespace).Update(ctx, statefulSet, metav1.UpdateOptions{})
	if err != nil {
		return fmt.Errorf("failed to scale statefulset: %w", err)
	}

	c.logger.Info("StatefulSet scaled",
		zap.String("statefulset", name),
		zap.Int32("replicas", replicas),
	)
	return nil
}

func (c *K8sClient) WaitForPVCReady(ctx context.Context, namespace, pvcName string, timeout time.Duration) error {
	deadline := time.Now().Add(timeout)
	for time.Now().Before(deadline) {
		pvc, err := c.GetPVC(ctx, namespace, pvcName)
		if err != nil {
			return err
		}

		if pvc.Status.Phase == corev1.ClaimBound {
			c.logger.Info("PVC is bound",
				zap.String("pvc", pvcName),
			)
			return nil
		}

		time.Sleep(5 * time.Second)
	}

	return fmt.Errorf("timeout waiting for PVC %s to be ready", pvcName)
}

func (c *K8sClient) GetStorageClass(ctx context.Context, name string) (*storagev1.StorageClass, error) {
	return c.clientset.StorageV1().StorageClasses().Get(ctx, name, metav1.GetOptions{})
}

func (c *K8sClient) ListStorageClasses(ctx context.Context) ([]storagev1.StorageClass, error) {
	scs, err := c.clientset.StorageV1().StorageClasses().List(ctx, metav1.ListOptions{})
	if err != nil {
		return nil, err
	}
	return scs.Items, nil
}

func (c *K8sClient) ListPVCs(ctx context.Context, namespace string) ([]corev1.PersistentVolumeClaim, error) {
	pvcs, err := c.clientset.CoreV1().PersistentVolumeClaims(namespace).List(ctx, metav1.ListOptions{})
	if err != nil {
		return nil, fmt.Errorf("failed to list PVCs: %w", err)
	}
	return pvcs.Items, nil
}
