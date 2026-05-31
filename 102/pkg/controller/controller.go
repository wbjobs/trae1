package controller

import (
	"context"
	"fmt"
	"net"
	"sync"
	"time"

	corev1 "k8s.io/api/core/v1"
	networkingv1 "k8s.io/api/networking/v1"
	metav1 "k8s.io/apimachinery/pkg/apis/meta/v1"
	"k8s.io/apimachinery/pkg/fields"
	"k8s.io/apimachinery/pkg/labels"
	"k8s.io/client-go/kubernetes"
	"k8s.io/client-go/tools/cache"
	"k8s.io/client-go/util/workqueue"

	"github.com/xdp-k8s-accel/pkg/bpf"
)

type Controller struct {
	clientset   *kubernetes.Clientset
	xdpAccel  *bpf.XDPAccel
	queue     workqueue.RateLimitingInterface
	podInformer cache.SharedIndexInformer
	npInformer  cache.SharedIndexInformer
	pods        map[string]*corev1.Pod
	rules       map[uint32]networkPolicyRule
	ruleID       uint32
	mu           sync.RWMutex
}

type networkPolicyRule struct {
	npName string
	keys   []bpf.RuleKey
}

func NewController(clientset *kubernetes.Clientset, xdpAccel *bpf.XDPAccel) *Controller {
	return &Controller{
		clientset: clientset,
		xdpAccel:  xdpAccel,
		queue:     workqueue.NewNamedRateLimitingQueue(workqueue.DefaultControllerRateLimiter(), "xdp-accel"),
		pods:      make(map[string]*corev1.Pod),
		rules:     make(map[uint32]networkPolicyRule),
	}
}

func (c *Controller) Run(ctx context.Context) error {
	podListWatcher := cache.NewListWatchFromClient(
		c.clientset.CoreV1().RESTClient(),
	"pods",
	corev1.NamespaceAll,
	fields.Everything(),
	)

	npListWatcher := cache.NewListWatchFromClient(
		c.clientset.NetworkingV1().RESTClient(),
	"networkpolicies",
	corev1.NamespaceAll,
	fields.Everything(),
	)

	c.podInformer = cache.NewSharedIndexInformer(
		podListWatcher,
		&corev1.Pod{},
		time.Minute*5,
		cache.Indexers{},
	)

	c.npInformer = cache.NewSharedIndexInformer(
		npListWatcher,
		&networkingv1.NetworkPolicy{},
		time.Minute*5,
		cache.Indexers{},
	)

	c.podInformer.AddEventHandler(cache.ResourceEventHandlerFuncs{
		AddFunc:    c.handlePodAdd,
		UpdateFunc: c.handlePodUpdate,
		DeleteFunc: c.handlePodDelete,
	})

	c.npInformer.AddEventHandler(cache.ResourceEventHandlerFuncs{
		AddFunc:    c.handleNetworkPolicyAdd,
		UpdateFunc: c.handleNetworkPolicyUpdate,
		DeleteFunc: c.handleNetworkPolicyDelete,
	})

	go c.podInformer.Run(ctx.Done())
	go c.npInformer.Run(ctx.Done())

	if !cache.WaitForCacheSync(ctx.Done(), c.podInformer.HasSynced, c.npInformer.HasSynced) {
		return fmt.Errorf("timed out waiting for caches to sync")
	}

	go c.worker(ctx)

	<-ctx.Done()
	return nil
}

func (c *Controller) handlePodAdd(obj interface{}) {
	pod, ok := obj.(*corev1.Pod)
	if !ok {
		return
	}
	c.mu.Lock()
	c.pods[podKey(pod)] = pod
	c.mu.Unlock()
	c.queue.Add("reconcile")
}

func (c *Controller) handlePodUpdate(oldObj, newObj interface{}) {
	c.handlePodAdd(newObj)
}

func (c *Controller) handlePodDelete(obj interface{}) {
	pod, ok := obj.(*corev1.Pod)
	if !ok {
		return
	}
	c.mu.Lock()
	delete(c.pods, podKey(pod))
	c.mu.Unlock()
	c.queue.Add("reconcile")
}

func (c *Controller) handleNetworkPolicyAdd(obj interface{}) {
	c.queue.Add("reconcile")
}

func (c *Controller) handleNetworkPolicyUpdate(oldObj, newObj interface{}) {
	c.queue.Add("reconcile")
}

func (c *Controller) handleNetworkPolicyDelete(obj interface{}) {
	c.queue.Add("reconcile")
}

func (c *Controller) worker(ctx context.Context) {
	for c.processNextWorkItem(ctx) {
	}
}

func (c *Controller) processNextWorkItem(ctx context.Context) bool {
	obj, shutdown := c.queue.Get()
	if shutdown {
		return false
	}
	defer c.queue.Done(obj)

	if err := c.reconcile(ctx); err != nil {
		c.queue.AddRateLimited(obj)
		return true
	}

	c.queue.Forget(obj)
	return true
}

func (c *Controller) reconcile(ctx context.Context) error {
	c.mu.Lock()
	defer c.mu.Unlock()

	for ruleID, rule := range c.rules {
		for _, key := range rule.keys {
			c.xdpAccel.DeleteRule(key)
		}
		delete(c.rules, ruleID)
	}
	c.ruleID = 0

	nps, err := c.clientset.NetworkingV1().NetworkPolicies(corev1.NamespaceAll).List(ctx, metav1.ListOptions{})
	if err != nil {
		return err
	}

	for _, np := range nps.Items {
		c.processNetworkPolicy(&np)
	}

	return nil
}

func (c *Controller) processNetworkPolicy(np *networkingv1.NetworkPolicy) {
	ruleID := c.ruleID
	c.ruleID++

	var keys []bpf.RuleKey

	for _, ingress := range np.Spec.Ingress {
		for _, from := range ingress.From {
			if from.PodSelector != nil {
				for _, pod := range c.pods {
					if podMatchesSelector(pod, from.PodSelector) {
						for _, port := range ingress.Ports {
							key := c.buildRuleKey(pod.Status.PodIP, "", port)
							keys = append(keys, key)
							value := bpf.RuleValue{Action: 0, RuleID: ruleID}
							c.xdpAccel.AddRule(key, value)
						}
					}
				}
			}
		}
	}

	for _, egress := range np.Spec.Egress {
		for _, to := range egress.To {
			if to.PodSelector != nil {
				for _, pod := range c.pods {
					if podMatchesSelector(pod, to.PodSelector) {
						for _, port := range egress.Ports {
							key := c.buildRuleKey("", pod.Status.PodIP, port)
							keys = append(keys, key)
							value := bpf.RuleValue{Action: 0, RuleID: ruleID}
							c.xdpAccel.AddRule(key, value)
						}
					}
				}
			}
		}
	}

	c.rules[ruleID] = networkPolicyRule{
		npName: np.Name,
		keys:   keys,
	}
}

func (c *Controller) buildRuleKey(srcIP, dstIP string, port networkingv1.NetworkPolicyPort) bpf.RuleKey {
	key := bpf.RuleKey{}

	if srcIP != "" {
		if ip := net.ParseIP(srcIP); ip != nil {
			key.SrcIP = bpf.IPToUint32(ip)
		}
	}

	if dstIP != "" {
		if ip := net.ParseIP(dstIP); ip != nil {
			key.DstIP = bpf.IPToUint32(ip)
		}
	}

	if port.Port != nil {
		key.DstPort = uint16(port.Port.IntVal)
	}

	if port.Protocol != nil {
		switch *port.Protocol {
		case corev1.ProtocolTCP:
			key.Protocol = 6
		case corev1.ProtocolUDP:
			key.Protocol = 17
		case corev1.ProtocolSCTP:
			key.Protocol = 132
		}
	}

	return key
}

func podKey(pod *corev1.Pod) string {
	return fmt.Sprintf("%s/%s", pod.Namespace, pod.Name)
}

func podMatchesSelector(pod *corev1.Pod, selector *metav1.LabelSelector) bool {
	if selector == nil {
		return true
	}
	sel, err := metav1.LabelSelectorAsSelector(selector)
	if err != nil {
		return false
	}
	return sel.Matches(labels.Set(pod.Labels))
}
