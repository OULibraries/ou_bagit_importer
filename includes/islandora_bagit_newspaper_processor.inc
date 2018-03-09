<?php

/* 
 * This class is specific for newspaper module
 */

class IslandoraBagitNewspaperProcessor extends IslandoraBagitProcessor {
    
    function __construct($uri, $parent_collection, $pid_namespace)
    {
        parent::__construct($uri, $parent_collection, $pid_namespace);
    }
    
    function batchImportFromRecipeData(){
        $this->prepareImportDataFromBagit();
        $recipe_data = $this->getRecipeData();
        $newspaper = $this->updateIslandoraCollectionFromRecipeData($this->getParentCollection(), $recipe_data['recipe']);
        $issues = $recipe_data['recipe']['issues'];
        foreach ($issues as $sequence_number => $issue_data){
            $issue = $this->updateIslandoraCollectionFromRecipeData($newspaper->id, $issue_data, $sequence_number+1);
            if(count($pages = $issue_data['pages']) > 0){
                foreach ($pages as $page_number => $page_data){
                    $page = $this->updateIslandoraCollectionFromRecipeData($issue->id, $page_data, $page_number+1);
                }
            }
        }
        $this->outputResults($result);
        drupal_set_message(t('Saving book collection @book', array('@book' => $object['uuid'])));
    }
    
}
