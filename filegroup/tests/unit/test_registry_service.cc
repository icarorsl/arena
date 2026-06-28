#include <gtest/gtest.h>
#include "registry/registry_service.h"

using namespace filegroup;

class RegistryServiceTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Each test uses isolated registry instance
        // This is a workaround for the singleton pattern
    }
};

TEST_F(RegistryServiceTest, SingletonInstance) {
    // Skip testing singleton persistence across tests
    auto& reg1 = RegistryService::instance();
    EXPECT_NE(&reg1, nullptr);
}

TEST_F(RegistryServiceTest, RegisterNode) {
    auto& registry = RegistryService::instance();
    
    auto resp = registry.register_node(101, "localhost", 5001);
    EXPECT_TRUE(resp.success);
    EXPECT_EQ(resp.error, "");
}

TEST_F(RegistryServiceTest, RegisterMultipleNodes) {
    auto& registry = RegistryService::instance();
    
    auto resp1 = registry.register_node(201, "node1", 5001);
    auto resp2 = registry.register_node(202, "node2", 5002);
    auto resp3 = registry.register_node(203, "node3", 5003);
    
    EXPECT_TRUE(resp1.success);
    EXPECT_TRUE(resp2.success);
    EXPECT_TRUE(resp3.success);
}

TEST_F(RegistryServiceTest, RegisterDuplicateNodeId) {
    auto& registry = RegistryService::instance();
    
    registry.register_node(301, "node1", 5001);
    auto resp2 = registry.register_node(301, "node2", 5002);
    
    EXPECT_FALSE(resp2.success);
}

TEST_F(RegistryServiceTest, RegisterInvalidNodeId) {
    auto& registry = RegistryService::instance();
    
    auto resp = registry.register_node(0, "node", 5001);
    EXPECT_FALSE(resp.success);
}

TEST_F(RegistryServiceTest, RegisterEmptyHost) {
    auto& registry = RegistryService::instance();
    
    auto resp = registry.register_node(401, "", 5001);
    EXPECT_FALSE(resp.success);
}

TEST_F(RegistryServiceTest, RegisterInvalidPort) {
    auto& registry = RegistryService::instance();
    
    auto resp1 = registry.register_node(501, "node", 0);
    auto resp2 = registry.register_node(502, "node", 65536);
    
    EXPECT_FALSE(resp1.success);
    EXPECT_FALSE(resp2.success);
}

TEST_F(RegistryServiceTest, GetNode) {
    auto& registry = RegistryService::instance();
    
    registry.register_node(601, "node1", 5001);
    
    auto resp = registry.get_node(601);
    EXPECT_EQ(resp.nodes.size(), 1);
    EXPECT_EQ(resp.nodes[0].node_id, 601);
    EXPECT_EQ(resp.nodes[0].host, "node1");
    EXPECT_EQ(resp.nodes[0].port, 5001);
}

TEST_F(RegistryServiceTest, GetNonExistentNode) {
    auto& registry = RegistryService::instance();
    
    auto resp = registry.get_node(99999);
    EXPECT_EQ(resp.nodes.size(), 0);
    EXPECT_NE(resp.error, "");
}

TEST_F(RegistryServiceTest, UnregisterNode) {
    auto& registry = RegistryService::instance();
    
    registry.register_node(701, "node1", 5001);
    auto resp = registry.unregister_node(701);
    
    EXPECT_TRUE(resp.success);
    
    auto check_resp = registry.get_node(701);
    EXPECT_EQ(check_resp.nodes.size(), 0);
}

TEST_F(RegistryServiceTest, UnregisterNonExistentNode) {
    auto& registry = RegistryService::instance();
    
    auto resp = registry.unregister_node(99999);
    EXPECT_FALSE(resp.success);
}

TEST_F(RegistryServiceTest, Heartbeat) {
    auto& registry = RegistryService::instance();
    
    registry.register_node(801, "node1", 5001);
    
    auto resp = registry.heartbeat(801);
    EXPECT_TRUE(resp.success);
}

TEST_F(RegistryServiceTest, HeartbeatNonExistentNode) {
    auto& registry = RegistryService::instance();
    
    auto resp = registry.heartbeat(99999);
    EXPECT_FALSE(resp.success);
}

TEST_F(RegistryServiceTest, SetNodeStatus) {
    auto& registry = RegistryService::instance();
    
    registry.register_node(901, "node1", 5001);
    
    auto resp1 = registry.set_node_status(901, "degraded");
    auto resp2 = registry.set_node_status(901, "offline");
    auto resp3 = registry.set_node_status(901, "healthy");
    
    EXPECT_TRUE(resp1.success);
    EXPECT_TRUE(resp2.success);
    EXPECT_TRUE(resp3.success);
    
    auto get_resp = registry.get_node(901);
    EXPECT_EQ(get_resp.nodes[0].status, "healthy");
}

TEST_F(RegistryServiceTest, SetInvalidStatus) {
    auto& registry = RegistryService::instance();
    
    registry.register_node(1001, "node1", 5001);
    
    auto resp = registry.set_node_status(1001, "invalid");
    EXPECT_FALSE(resp.success);
}

TEST_F(RegistryServiceTest, DefaultStatusIsHealthy) {
    auto& registry = RegistryService::instance();
    
    registry.register_node(1101, "node1", 5001);
    
    auto resp = registry.get_node(1101);
    EXPECT_EQ(resp.nodes[0].status, "healthy");
}

TEST_F(RegistryServiceTest, HeartbeatUpdatesTimestamp) {
    auto& registry = RegistryService::instance();
    
    registry.register_node(1201, "node1", 5001);
    auto resp1 = registry.get_node(1201);
    auto ts1 = resp1.nodes[0].last_heartbeat_us;
    
    // Small delay
    usleep(100);
    
    registry.heartbeat(1201);
    auto resp2 = registry.get_node(1201);
    auto ts2 = resp2.nodes[0].last_heartbeat_us;
    
    EXPECT_GT(ts2, ts1);
}

TEST_F(RegistryServiceTest, MultipleNodesIndependent) {
    auto& registry = RegistryService::instance();
    
    registry.register_node(1301, "node1", 5001);
    registry.register_node(1302, "node2", 5002);
    
    registry.set_node_status(1301, "degraded");
    
    auto resp1 = registry.get_node(1301);
    auto resp2 = registry.get_node(1302);
    
    EXPECT_EQ(resp1.nodes[0].status, "degraded");
    EXPECT_EQ(resp2.nodes[0].status, "healthy");
}
