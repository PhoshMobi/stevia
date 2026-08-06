all

rule 'MD013', :line_length => 1024, :ignore_code_blocks => true

# GTK doc has metadata at the top
exclude_rule 'MD041'
