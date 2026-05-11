#!/usr/bin/env python3

# init: 
# {"jsonrpc": "2.0", "id": 1, "method": "initialize", "params": {"protocolVersion": "2024-11-05", "capabilities": {}, "clientInfo": {"name": "test-client", "version": "1.0.0"}}}
# call:
# {"jsonrpc": "2.0", "id": 2, "method": "tools/call", "params": {"name": "search_samples", "arguments": {"query": "repne movsw"}}}

import sys
import os
import time
import io
import logging

def log(f, message):
    f.write(f"[{time.strftime('%Y-%m-%d %H:%M:%S')}] {message}\n")
    f.flush()

# Create a log file in the same directory
LOG_PATH = os.path.join(os.path.dirname(__file__), "mcp_debug.log")
log_file = open(LOG_PATH, "w", encoding="utf-8")
log(log_file, "PROBE STARTED")
log(log_file, f"Python Executable: {sys.executable}")
log(log_file, f"Current Directory: {os.getcwd()}")

# Force the working directory to be the script's location, this ensures it finds 'rag_common.py' and the database
# script_dir = os.path.dirname(os.path.abspath(__file__))
# os.chdir(script_dir)
# sys.path.append(script_dir)
# Prevent log pollution on stdout
logging.basicConfig(level=logging.ERROR, stream=sys.stderr)
# Fix Windows UTF-8/Buffering
sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding='utf-8', write_through=True)

import lancedb
import sys
from mcp.server.fastmcp import FastMCP
from rag_common import *

mcp = FastMCP("examples")
# Initialize vector database connection
db = lancedb.connect(db_name)
table = db.open_table(table_name)

@mcp.tool()
async def search(query: str, limit: int = 3) -> str:
    """
    Finds 16-bit x86 assembly patterns and their corresponding C code. 
    Use this tool when you encounter:
    - Unknown library calls or subroutines.
    - Complex pointer arithmetic (BP/BX offsets).
    - DOS-specific interrupt logic or hardware register access.
    
    The query should be a representative string of assembly instructions or 
    a description of the logic (e.g., 'file read loop' or 'segmented string copy').
    """    
    log(log_file, f"--- Querying database with: '{query}', limit={limit}")
    # Query vector database
    results = table.search(query).limit(limit).to_list()
    # Format results for Continue
    output = []
    for result in results:
        output.append(
            f"ASSEMBLY:\n{result['asm_code']}"
            f"C EQUIVALENT:\n{result['c_code']}"
        )
    response = "\n".join(output)
    log(log_file, f"Database responding with match: '{response}'")
    return response

def probe():
    # Force stdin/stdout to be unbuffered binary for raw pipe capture
    # On Windows, this prevents the \r\n translation issues
    try:
        while True:
            line = sys.stdin.readline()
            if not line:
                log(log_file, "PIPE CLOSED (EOF)")
                break
            
            log(log_file, f"RECEIVED: {line.strip()}")
            
            # If we see an 'initialize' request, we MUST respond with 
            # some valid JSON-RPC or Continue will kill the process.
            if '"method":"initialize"' in line:
                # Minimal valid MCP initialization response
                response = '{"jsonrpc":"2.0","id":1,"result":{"protocolVersion":"2024-11-05","capabilities":{},"serverInfo":{"name":"probe","version":"1.0"}}}\n'
                sys.stdout.write(response)
                sys.stdout.flush()
                log(log_file, f"SENT: {response.strip()}")

    except Exception as e:
        log(log_file, f"CRASH: {str(e)}")

if __name__ == "__main__":
    if len(sys.argv) > 1 and sys.argv[1] == '--probe':
        probe()
    else:
        mcp.run()
    log(log_file, "Terminating server")
