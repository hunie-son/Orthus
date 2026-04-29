#!/usr/bin/env bash
set -e


# This is for docker cleanup! 
#

echo "Docker disk usage before cleanup"
docker system df

echo
echo "Pruning build cache"
docker builder prune -af

echo
echo "Pruning images"
docker image prune -af

echo
echo "Pruning volumes"
docker volume prune -f

echo
echo "Pruning system (including volumes)"
docker system prune -af --volumes

echo
echo "Docker disk usage after cleanup"
docker system df

