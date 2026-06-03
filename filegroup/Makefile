.PHONY: up down clean rebuild

# Full rebuild — always uses --no-cache to avoid Docker layer caching issues
up:
	docker compose down -v
	docker compose build --no-cache engine
	docker compose up -d
	@echo "Engine running on :8443, metrics on :9090"

# Stop without destroying volume
down:
	docker compose down

# Wipe everything
clean:
	docker compose down -v

# Rebuild engine only (keeps volume)
rebuild:
	docker compose build --no-cache engine
	docker compose up -d

# Dashboard
dash:
	kill $$(lsof -ti:5001) 2>/dev/null; sleep 1
	cd ../clients/csharp && dotnet run --project WebDashboard --urls http://localhost:5001 &
	@sleep 3
	@curl -s -o /dev/null -w 'Dashboard: HTTP %{http_code}\n' http://localhost:5001/
