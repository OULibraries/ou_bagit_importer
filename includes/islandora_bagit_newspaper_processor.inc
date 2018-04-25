<?php

/* 
 * This class is specific for newspaper module
 */

class IslandoraBagitNewspaperProcessor extends IslandoraBagitProcessor {
    
    function __construct($uri, $parent_collection, $pid_namespace, $recipe_data, $tmp_dir)
    {
        parent::__construct($uri, $parent_collection, $pid_namespace, $recipe_data, $tmp_dir);
    }
    
    function batchImportFromRecipeData(){
        $this->prepareImportDataFromBagit();
        $recipe_data = $this->getRecipeData();
        $newspaper = $this->updateIslandoraCollectionFromRecipeData($this->getParentCollection(), $recipe_data['recipe']);
        if(empty($newspaper)){
            drupal_set_message("Cannot add new newspaper ".($recipe_data['recipe']['label']));
            throw new Exception("Cannot add new newspaper ".($recipe_data['recipe']['label']));
        }
        $issues = $recipe_data['recipe']['issues'];
        
        $batch = array(
            'operations' => array(),
            'finished' => '_ou_bagit_importer_batches_finished',
            'title' => dt('Ingesting BAG...'),
            'init_message' => dt('Preparing to ingest BAG...'),
            'progress_message' => dt('Ingesting BAG'),
            'error_message' => dt('Page could not get ingested'),
        );
        
        foreach ($issues as $sequence_number => $issue_data){
            $issue = $this->updateIslandoraCollectionFromRecipeData($newspaper->id, $issue_data, $sequence_number+1);
            if(empty($issue)){
                drupal_set_message("Cannot add new issue with sequence_number=".($sequence_number+1));
                continue;
            }
            if(count($pages = $issue_data['pages']) > 0){
                foreach ($pages as $page_number => $page_data){
                    $batch['operations'][] = array(
                        'ou_bagit_importer_batch_import_page_by_processor',
                        array(serialize($this), $issue->id, $page_data, $page_number+1),
                    );
                }
            }
        }
        
        batch_set($batch);
        drupal_set_message(t('Page processing has been done.'));
    }
    
}
