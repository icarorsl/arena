.PHONY: up down clean rebuild verify dash

# Build and start the 3-node cluster (node1 :8443, node2 :8444, node3 :8445)
up:
	docker compose down -v
	docker compose up -d --build
	@echo "Cluster up — node1 :8443, node2 :8444, node3 :8445"

# Stop without destroying volumes
down:
	docker compose down

# Wipe everything
clean:
	docker compose down -v

# Rebuild images (keeps volumes)
rebuild:
	docker compose build --no-cache
	docker compose up -d

# Upload to node1 and read the file back from node2 — verifies that metadata
# and chunks replicated across the cluster.
verify:
	cd ../clients/csharp && dotnet run --project DemoLocal -- http://localhost:8443 http://localhost:8444

# Dashboard
dash:
	kill $$(lsof -ti:5001) 2>/dev/null; sleep 1
	cd ../clients/csharp && dotnet run --project WebDashboard --urls http://localhost:5001 &
	@sleep 3
	@curl -s -o /dev/null -w 'Dashboard: HTTP %{http_code}\n' http://localhost:5001/
