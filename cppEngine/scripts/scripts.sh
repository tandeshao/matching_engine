#!/bin/bash
i=1
for file in customTests/*
do
    output=$(../grader ../engine < "$file" 2>&1 | tail -1 | tr -d '\0')
    if [ "$output" == "test passed." ]
    then
        echo "Test case $i: ✓"
    else
        "Test case $i: $output"
    fi
    i=$((i+1))
done