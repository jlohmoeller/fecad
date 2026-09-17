
antlr_target(SQLGrammarLexer SQLLexer.g4 LEXER
        PACKAGE pat_disc)

antlr_target(SQLGrammarParser SQLParser.g4 PARSER
        PACKAGE pat_disc
        DEPENDS_ANTLR SQLGrammarLexer
        COMPILE_FLAGS -lib ${ANTLR_SQLGrammarLexer_OUTPUT_DIR}
        VISITOR)

add_custom_target(sql_grammar DEPENDS ${ANTLR_SQLGrammarLexer_OUTPUTS} ${ANTLR_SQLGrammarParser_OUTPUTS})

set(ANTLR_SQLGrammarLexer_CXX_OUTPUTS ${ANTLR_SQLGrammarLexer_CXX_OUTPUTS} PARENT_SCOPE)
set(ANTLR_SQLGrammarParser_CXX_OUTPUTS ${ANTLR_SQLGrammarParser_CXX_OUTPUTS} PARENT_SCOPE)
set(ANTLR_SQLGrammarLexer_OUTPUT_DIR ${ANTLR_SQLGrammarLexer_OUTPUT_DIR} PARENT_SCOPE)
set(ANTLR_SQLGrammarParser_OUTPUT_DIR ${ANTLR_SQLGrammarParser_OUTPUT_DIR} PARENT_SCOPE)