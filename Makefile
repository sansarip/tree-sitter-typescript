update-js-grammar:
	@echo '➕ Adding JS grammar dependency'
	yarn add github:sansarip/tree-sitter-javascript || true
	@echo '🧹 Cleaning files'
	rm typescript/src/grammar.json || true
	rm typescript/src/node-types.json || true
	rm typescript/src/parser.c || true
	rm tsx/src/grammar.json || true
	rm tsx/src/node-types.json || true
	rm tsx/src/parser.c || true
	@echo '🏭 Regenerating'
	cd typescript && tree-sitter generate
	cd tsx && tree-sitter generate
	@echo '📦 Building WASMs'
	cd typescript && tree-sitter build-wasm
	cd tsx && tree-sitter build-wasm
	
