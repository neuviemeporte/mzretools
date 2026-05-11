#!/usr/bin/env python3
import lancedb
import sys
import re
import textwrap
from lancedb.pydantic import LanceModel, Vector
from lancedb.embeddings import get_registry
from output import *
from rag_common import *

def syntax():
    print("Syntax: rag_index.py [--debug] <input_file>")
    print("This takes a file with sample assembly + C code equivalents, splits it into chunks,\n"
          "runs them through an embedding model and appends the embeddings to LanceDB")
    sys.exit(1)

if len(sys.argv) < 2:
    syntax()
sys.argv.pop(0)

def add_sample(samples, depth_samples, depth, asm_code, c_code):
    if not asm_code or not c_code:
        return
    # Use textwrap.dedent to remove 'hanging' indentation  but keep the relative indentation between lines.
    clean_c = textwrap.dedent(c_code)
    clean_asm = textwrap.dedent(asm_code)
    debug(f"Adding sample (depth {depth}):\n--- asm:\n{clean_asm}--- c:\n{clean_c}---")
    samples.append({"asm_code": asm_code, "c_code": clean_c})
    if depth + 1 in depth_samples:
        # depth reduction, previous depth's sample is complete, add it to the global sample set if eligible based on line count
        sample = depth_samples[depth + 1]
        depth_asm = sample["asm_code"] + asm_code
        depth_c = textwrap.dedent(sample["c_code"] + c_code)
        line_count = len(depth_asm.splitlines()) + len(depth_c.splitlines())
        if line_count < line_limit:
            debug(f"Adding depth reduction sample (depth {depth + 1}, line count {line_count}):\n--- asm:\n{depth_asm}--- c:\n{depth_c}---")
            samples.append({"asm_code": depth_asm, "c_code": depth_c})
        else:
            debug(f"Rejecting depth reduction sample (depth {depth + 1}, line count {line_count}):\n--- asm:\n{depth_asm}--- c:\n{depth_c}---")
        # remove depth samples for all depths deeper than the one just reduced - no need to keep anymore as we just stepped out of the enclosing one
        for k in [x for x in depth_samples.keys() if x > depth]:
            debug(f"Deleting sample for depth {k}")
            del depth_samples[k]
    if depth != 0:
        # add this code sample to all depths below and including the current one
        for d in range(1, depth + 1):
            if not d in depth_samples:
                debug(f"Creating sample for depth {d}")
                depth_samples[d] = {"asm_code": '', "c_code": ''}
            depth_samples[d]["asm_code"] += asm_code
            depth_samples[d]["c_code"] += c_code

offset_re = re.compile(r'^[a-zA-Z0-9]+:[a-fA-F0-9]+\s')
comment_re = re.compile(r'\s*//')
code_samples = []
depth_samples = {}
file_count = 0
while sys.argv:
    input_file = sys.argv.pop(0)
    if input_file == '--debug':
        setDebug(True)
        continue
    old_count = len(code_samples)
    # extract code samples from the input file, expecting disassembly in /*...*/ comments, followed by C code
    with open(input_file, "r") as file:
        file_count += 1
        in_asm = False
        asm_code = ''
        c_code = ''
        c_depth = 0
        lineno = 0
        blocks = {}
        for line in file:
            lineno += 1
            line_stripped = line.strip()
            if not line_stripped:
                # ignore empty lines
                continue
            elif line_stripped == '/*':
                # beginning of comment block, assembly code follows 
                # add previous code to code_samples if any 
                add_sample(code_samples, depth_samples, c_depth, asm_code, c_code)
                asm_code = ''
                c_code = ''
                in_asm = True
            elif line_stripped == '*/':
                # end of comment block, c code follows
                in_asm = False
                
            elif in_asm:
                debug(f"Appending assembly code to asm_code: '{line_stripped}'")
                asm_code += offset_re.sub('', line)
            else:
                # ignore C // comment lines (offset markings, might confuse model)
                if comment_re.match(line):
                    continue
                cur_depth = c_depth + line.count('{') - line.count('}')
                if cur_depth < 0:
                    error(f"Negative indentation on line {lineno}: '{line}'")
                debug(f"Appending c code to c_code: '{line_stripped}', depth: {cur_depth}")
                c_depth = cur_depth
                c_code += line
        # final item after eof
        add_sample(code_samples, depth_samples, c_depth, asm_code, c_code)
        info(f"Loaded {len(code_samples) - old_count} samples from {input_file}")

info(f"Loaded {len(code_samples)} samples in total from {file_count} files")

# choose embedding model
model = get_registry().get(embed_provider).create(name=embed_model, host=model_host)

# define the schema
class DecompilationPair(LanceModel):
    # This stores the ASM. 'SourceField' tells LanceDB to vectorize this text.
    asm_code: str = model.SourceField()
    # This stores the C. We don't necessarily need to vectorize the C, 
    # just retrieve it as a "Payload".
    c_code: str
    # The vector itself (automatically generated by LanceDB using nomic)
    vector: Vector(model.ndims()) = model.VectorField()

# create the table if it doesn't exist and add the embedded data
db = lancedb.connect(db_name)
info(f"Creating new table {table_name} with schema...")
table = db.create_table(table_name, schema=DecompilationPair, mode="overwrite")
table.add(code_samples)
rows = table.count_rows()
info(f"Table now holding {rows} rows")